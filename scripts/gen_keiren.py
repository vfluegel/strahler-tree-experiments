#!/usr/bin/env python3
"""Run Keiren benchmarks one game at a time, or render their saved metrics offline."""

import argparse
import bz2
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from copy import copy
import csv
from dataclasses import dataclass
import fcntl
import http.client
import html
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import platform
import re
import resource
import shutil
import signal
from statistics import median
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zipfile

import gen_decomp as decomp


# Ascending archive size, with the immutable v1 Figshare file identifiers.
ARCHIVES = {
    "mlsolver": 10792904,
    "pgsolver": 10792910,
    "equivchecking-hesselink": 10811282,
    "modelchecking": 10792907,
    "equivchecking": 10811291,
}
FIELDS = ["archive", *decomp.SUMMARY_FIELDS, "seconds", "peak_rss_mib", "detail",
          "start_winner", "reference_solution", "reference_agrees", "winner_disagrees",
          "timeout_seconds", "memory_gib", "jobs", "max_host_leaves", "machine", "solver_build"]
GAME_FIELDS = ("vertices", "edges", "distinct_priorities", "maximum_priority")
# pg2adot's parse_size accepts SIZE_MAX.
MAX_HOST_LEAVES = 2 * sys.maxsize + 1
OUTCOMES = ("complete", "partial", "host limit", "timeout", "out of memory",
            "crash", "error", "invalid input", "download failed")
SOURCE = (
    "Source: [Parity games](https://figshare.com/articles/dataset/Parity_games/6004130). "
    "[DOI: 10.6084/m9.figshare.6004130.v1]"
    "(https://doi.org/10.6084/m9.figshare.6004130.v1).\n\n"
    "Keiren, Jeroen (2018). Parity games. figshare. Dataset. CC0. "
    "[Benchmarks for Parity Games (FSEN 2015)](https://arxiv.org/abs/1407.3121); "
    "[generator](https://github.com/jkeiren/paritygame-generator).\n\n"
)
SCRIPT = str(Path(__file__).resolve())
NETWORK_ERRORS = (OSError, urllib.error.URLError, http.client.HTTPException,
                  EOFError, zipfile.BadZipFile)


def retry(operation):
    for attempt in range(3):
        try:
            return operation()
        except NETWORK_ERRORS as error:
            if isinstance(error, urllib.error.HTTPError):
                error.close()
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)


def download_url(file_id):
    return f"https://ndownloader.figshare.com/files/{file_id}"


class RangeReader(io.RawIOBase):
    """Seek over HTTP ranges; never accept an accidental full-archive response."""

    def __init__(self, url):
        super().__init__()
        self.source = self.url = url
        self.pos = 0
        self.size = None
        self._fetch(0, 0)

    def _fetch(self, start, end):
        def request():
            try:
                req = urllib.request.Request(self.url, headers={
                    "Range": f"bytes={start}-{end}", "Accept-Encoding": "identity"})
                with urllib.request.urlopen(req, timeout=60) as response:
                    span = re.fullmatch(r"bytes (\d+)-(\d+)/(\d+)",
                                        response.headers.get("Content-Range", ""))
                    if (response.status != 206 or not span or
                            tuple(map(int, span.group(1, 2))) != (start, end)):
                        raise OSError("server did not honor the requested HTTP range")
                    size = int(span[3])
                    if self.size is not None and self.size != size:
                        raise OSError("archive size changed during download")
                    data = response.read(end - start + 2)
                    if len(data) != end - start + 1:
                        raise OSError("short HTTP range response")
                    self.size = size
                    self.url = response.geturl()
                    return data
            except NETWORK_ERRORS:
                # Signed S3 redirects expire; retry through Figshare for a fresh URL.
                self.url = self.source
                raise
        return retry(request)

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, offset, whence=os.SEEK_SET):
        if whence not in (os.SEEK_SET, os.SEEK_CUR, os.SEEK_END):
            raise ValueError("invalid whence")
        pos = offset + (self.pos if whence == os.SEEK_CUR else
                        self.size if whence == os.SEEK_END else 0)
        if pos < 0:
            raise ValueError("negative seek")
        self.pos = pos
        return pos

    def readinto(self, buffer):
        if not buffer or self.pos >= self.size:
            return 0
        end = min(self.pos + len(buffer), self.size) - 1
        data = self._fetch(self.pos, end)
        buffer[:len(data)] = data
        self.pos += len(data)
        return len(data)


@contextmanager
def remote_zip(file_id):
    with RangeReader(download_url(file_id)) as raw:
        with io.BufferedReader(raw, 1 << 20) as stream:
            with zipfile.ZipFile(stream) as archive:
                yield archive


def game_members(archive):
    return sorted((entry for entry in archive.infolist()
                   if not entry.is_dir() and entry.filename.endswith(".gm.bz2")
                   and "games" in PurePosixPath(entry.filename).parts
                   and "__MACOSX" not in PurePosixPath(entry.filename).parts),
                  key=lambda entry: entry.filename)


def open_member(archive, member):
    """Recover wrapped macOS ZIP64 offsets before letting zipfile read and check CRCs."""
    stream = archive.fp
    size = stream.seek(0, os.SEEK_END)
    wrap = 1 << 32
    offsets = dict.fromkeys([member.header_offset,
                            *range(member.header_offset % wrap, size, wrap)])
    encoding = "utf-8" if member.flag_bits & 0x800 else getattr(archive, "metadata_encoding", None) or "cp437"
    filename = member.orig_filename.encode(encoding)
    for offset in offsets:
        if not 0 <= offset <= size - 30:
            continue
        stream.seek(offset)
        header = stream.read(30)
        if header[:4] != b"PK\x03\x04" or len(header) != 30:
            continue
        if stream.read(int.from_bytes(header[26:28], "little")) != filename:
            continue
        member = copy(member)
        member.header_offset = offset
        # Wrapped offsets also corrupt boundaries of members whose own offsets are correct.
        member._end_offset = None
        # zipfile uses central-directory sizes/CRC even with data descriptors (flag 3).
        return archive.open(member)
    raise zipfile.BadZipFile(f"no matching local header for {member.filename}")


def extract_game(archive, member, destination):
    def extract():
        try:
            with open_member(archive, member) as compressed:
                with bz2.open(compressed, "rb") as source, destination.open("wb") as target:
                    shutil.copyfileobj(source, target, 1 << 20)
        except BaseException:
            destination.unlink(missing_ok=True)
            raise
    retry(extract)


def download_metadata(work):
    path = work / "data.yaml"
    def download():
        temporary = path.with_suffix(".yaml.part")
        try:
            with urllib.request.urlopen(download_url(10792901), timeout=60) as source:
                with temporary.open("wb") as target:
                    shutil.copyfileobj(source, target, 1 << 20)
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    retry(download)
    return path


def reference_solutions(path):
    """Read only file scalars and solutions/orig from the dataset's YAML records."""
    # shortcut: scalar mappings between files blocks; update if the v1 YAML schema changes.
    records, scalars, stack = [], [], []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip())
        content = line.lstrip()
        if content.startswith("- "):
            content = content[2:]
            indent += 2
        match = re.fullmatch(r"([^:]+):(?:\s+(.*))?", content)
        if not match:
            continue
        key, value = match[1].strip("'\" "), (match[2] or "").strip()
        if key == "files" and not value:
            if scalars:
                records.append(scalars)
            scalars, stack = [], []
        while stack and stack[-1][0] >= indent:
            stack.pop()
        keys = tuple(part[1] for part in stack) + (key,)
        if value:
            if value.startswith('"'):
                try:
                    value = json.loads(value)
                except ValueError:
                    continue
            elif value.startswith("'") and value.endswith("'"):
                value = value[1:-1].replace("''", "'")
            scalars.append((keys, value))
        else:
            stack.append((indent, key))
    if scalars:
        records.append(scalars)
    references = {}
    for record in records:
        solutions = {str(value).lower() for keys, value in record
                     if "solutions" in keys and "orig" in keys
                     and str(value).lower() in ("true", "false")}
        if len(solutions) != 1:
            continue
        solution, = solutions
        for keys, value in record:
            if "files" in keys and "orig" in keys:
                name = str(PurePosixPath(value)).removesuffix(".bz2")
                # data.yaml links structural metadata, e.g. cases/mlsolver/data/X.yaml.
                if name.startswith("cases/") and "/data/" in name and name.endswith(".yaml"):
                    name = name.removeprefix("cases/").replace("/data/", "/games/", 1)
                    name = name.removesuffix(".yaml") + ".gm"
                if name.endswith(".gm"):
                    references.setdefault(name, set()).add(solution)
    return {key: next(iter(values)) for key, values in references.items()
            if len(values) == 1}


def reference_for_game(references, game):
    solutions = {solution for path, solution in references.items()
                 if path == game or path.endswith("/" + game)}
    return next(iter(solutions)) if len(solutions) == 1 else ""


@dataclass
class ProcessResult:
    code: int
    peak_rss_mib: float
    peak_address_bytes: int = 0
    timed_out: bool = False


def execute(command, game, stdout, stderr, deadline, memory):
    """Reap this PID with wait4; Popen.wait would lose its individual rusage."""
    wrapper = [sys.executable, SCRIPT, "_exec", str(memory), *map(str, command)]
    with game.open("rb") as source, stdout.open("wb") as out, stderr.open("wb") as err:
        child = subprocess.Popen(wrapper, stdin=source, stdout=out, stderr=err,
                                 start_new_session=True, cwd=decomp.ROOT,
                                 env={**os.environ, "LC_ALL": "C", "LANG": "C"})
        peak_address = 0
        timed_out = False
        status = None
        def kill():
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        try:
            while True:
                pid, status, usage = os.wait4(child.pid, os.WNOHANG)
                if pid:
                    break
                if time.monotonic() >= deadline:
                    timed_out = True
                    kill()
                    _, status, usage = os.wait4(child.pid, 0)
                    break
                # Address space, unlike RSS, can show that a signal followed an AS cap.
                if sys.platform.startswith("linux"):
                    try:
                        text = Path(f"/proc/{child.pid}/status").read_text()
                        peaks = re.findall(r"^Vm(?:Peak|Size):\s+(\d+) kB", text, re.MULTILINE)
                        peak_address = max([peak_address, *(int(v) * 1024 for v in peaks)])
                    except (OSError, ValueError):
                        pass
                time.sleep(min(0.01, max(0, deadline - time.monotonic())))
        except BaseException:
            kill()
            _, status, usage = os.wait4(child.pid, 0)
            raise
        finally:
            # The child has already been reaped; prevent Popen from waiting again.
            if status is not None:
                child.returncode = os.waitstatus_to_exitcode(status)
        rss = usage.ru_maxrss / (1024 ** 2 if sys.platform == "darwin" else 1024)
        return ProcessResult(child.returncode, rss, peak_address, timed_out)


def classify(result, stderr, memory, *, solver=True):
    lines = [line.strip() for line in stderr.splitlines() if line.strip()]
    detail = next((line for line in reversed(lines)
                   if re.search(r"error|fail|alloc|memory|ENOMEM", line, re.IGNORECASE)),
                  next((line for line in reversed(lines)
                        if not re.match(r"^[\w+ -]+:\s+\w+=", line)), "no stderr"))
    if result.timed_out:
        return "timeout", "deadline exceeded"
    if re.search(r"out[- ]of[- ]memory|cannot allocate|failed to allocate|allocation fail|"
                 r"bad_alloc|MemoryError|ENOMEM", detail, re.IGNORECASE):
        return "out of memory", detail
    if result.code < 0:
        name = signal.Signals(-result.code).name
        cap_hit = (result.code in (-signal.SIGKILL, -signal.SIGABRT, -signal.SIGSEGV)
                   and max(result.peak_address_bytes, result.peak_rss_mib * 1024 ** 2) >= memory * 0.95)
        return ("out of memory", f"{name} after reaching memory cap") if cap_hit else ("crash", name)
    if solver and re.search(r"more than (?:the limit|\d+ leaves)", detail):
        return "host limit", detail
    if solver and result.code == 3:
        return "invalid input", f"exit 3: {detail}"
    if result.code:
        return "error", f"exit {result.code}: {detail}"
    if solver:
        outcomes = re.findall(r"\bresult=(complete|partial)\b", stderr)
        return (outcomes[-1], "") if outcomes else ("error", "missing result statistics")
    return "complete", ""


def read_stderr(path):
    # Errors at either end survive very verbose multi-round runs without loading GBs.
    with path.open("rb") as stream:
        head = stream.read(1 << 20)
        if stream.seek(0, os.SEEK_END) > len(head):
            stream.seek(max(len(head), stream.tell() - (1 << 20)))
            head += b"\n" + stream.read(1 << 20)
    return head.decode("utf-8", errors="replace")


def capped_json(mode, arguments, game, work, deadline, memory):
    out, err = work / f"{mode}.json", work / f"{mode}.err"
    result = execute([sys.executable, SCRIPT, mode, *map(str, arguments)],
                     game, out, err, deadline, memory)
    outcome, detail = classify(result, read_stderr(err), memory, solver=False)
    if outcome != "complete":
        return None, outcome, detail, result.peak_rss_mib
    try:
        return json.loads(out.read_text()), outcome, "", result.peak_rss_mib
    except (OSError, ValueError) as error:
        return None, "error", str(error), result.peak_rss_mib


def run_one(backend, game, info, args):
    start = time.monotonic()
    deadline, memory = start + args.timeout, int(args.memory * 1024 ** 3)
    row = dict(backend=backend.name)
    peak = 0
    try:
        with tempfile.TemporaryDirectory(prefix=backend.name + "-", dir=game.parent) as directory:
            work = Path(directory)
            out, err = work / "tree.dot", work / "solver.err"
            result = execute(decomp.backend_command(backend, game, args.build_dir,
                                                    args.max_host_leaves),
                             game, out, err, deadline, memory)
            peak = result.peak_rss_mib
            outcome, detail = classify(result, read_stderr(err), memory)
            if outcome in ("complete", "partial"):
                metrics, parsed, problem, parser_rss = capped_json(
                    "_metrics", [backend.name, out, err, json.dumps(info)],
                    game, work, deadline, memory)
                peak = max(peak, parser_rss)
                if parsed == "complete":
                    row.update(metrics)
                else:
                    outcome, detail = parsed, "metrics: " + problem
            row.update(outcome=outcome, detail=detail)
    except MemoryError:
        row.update(outcome="out of memory", detail="runner MemoryError")
    except Exception as error:
        row.update(outcome="error", detail=f"{type(error).__name__}: {error}")
    seconds = time.monotonic() - start
    if seconds > args.timeout and row["outcome"] in ("complete", "partial"):
        row = {"backend": backend.name, "outcome": "timeout", "detail": "post-processing deadline exceeded"}
    row.update(seconds=f"{seconds:.3f}", peak_rss_mib=f"{peak:.3f}")
    return row


def load_results(path, *, allow_legacy=False):
    if not path.exists():
        return []
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        fields = [field for field in FIELDS if field != "solver_build"]
        legacy = reader.fieldnames in ([field for field in schema if field != "start_winner"]
                                      for schema in (FIELDS, fields))
        if legacy and not allow_legacy:
            raise ValueError(f"CSV lacks start_winner: {path}; delete or migrate it before resuming")
        if reader.fieldnames not in (FIELDS, fields) and not (legacy and allow_legacy):
            raise ValueError(f"unexpected CSV columns: {path}")
        rows = list(reader)
        if legacy:
            for row in rows:
                row["start_winner"] = ""
        if "solver_build" not in reader.fieldnames:
            for row in rows:
                row["solver_build"] = ""
    if any(None in row or any(value is None for value in row.values()) for row in rows):
        raise ValueError(f"incomplete CSV row: {path}")
    if any(row["archive"] not in ARCHIVES for row in rows):
        raise ValueError(f"unknown archive in CSV: {path}")
    return rows


def save_results(path, rows):
    """Publish the entire game's append with one replace, preserving the old CSV on failure."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="",
                                         dir=path.parent, prefix=".results-", delete=False) as stream:
            temporary = Path(stream.name)
            writer = csv.DictWriter(stream, fieldnames=FIELDS)
            writer.writeheader()
            writer.writerows(rows)
            os.fchmod(stream.fileno(), 0o644)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def missing_backends(rows, archive, game, backends):
    done = {row["backend"] for row in rows if row["archive"] == archive and
            row["game"] == game and row["outcome"] != "download failed"}
    return [backend for backend in backends if backend.name not in done]


def flag_winners(rows):
    complete = {(str(row.get("won_even", "")), str(row.get("won_odd", "")))
                for row in rows if row["outcome"] == "complete" and row.get("won_even", "") != ""}
    starts = {row["start_winner"] for row in rows if row["outcome"] == "complete"
              and row.get("start_winner") in ("even", "odd")}
    for row in rows:
        row["winner_disagrees"] = ("true" if len(complete) > 1 or len(starts) > 1 else "false") if row["outcome"] == "complete" else ""
        reference, winner = row.get("reference_solution"), row.get("start_winner")
        row["reference_agrees"] = (str((reference == "true") == (winner == "even")).lower()
                                   if reference in ("true", "false") and winner in ("even", "odd") else "")


def machine_info():
    cpus = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else os.cpu_count()
    try:
        ram = os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE") / 1024 ** 3
        return f"{platform.system()} {platform.machine()}, {cpus} CPUs, {ram:.1f} GiB RAM"
    except (OSError, ValueError):
        return f"{platform.system()} {platform.machine()}, {cpus} CPUs"


def solver_build(build_dir):
    version = subprocess.run([build_dir / "pg2adot", "--version"], check=True,
                             capture_output=True, text=True, timeout=10).stdout.splitlines()[0]
    try:
        result = subprocess.run(["meson", "introspect", "--buildoptions", build_dir],
                                check=True, capture_output=True, text=True, timeout=10)
        options = {option["name"]: option["value"] for option in json.loads(result.stdout)}
        return f"{version}, {options['buildtype']} -O{options['optimization']}"
    except (OSError, subprocess.SubprocessError, ValueError, KeyError):
        return version


@contextmanager
def game_metadata(archive, member, args):
    directory = retry(lambda: tempfile.TemporaryDirectory(prefix="game-", dir=args.work_dir))
    with directory:
        game = Path(directory.name) / "input.gm"
        extract_game(archive, member, game)
        info, status, detail, _ = capped_json(
            "_game_info", [game], game, Path(directory.name),
            time.monotonic() + args.metadata_timeout, int(args.memory * 1024 ** 3))
        if status != "complete":
            print(f"{member.filename}: metadata {status}: {detail}", file=sys.stderr)
        yield game, info, status, detail


def measure_game(archive, member, pending, args):
    try:
        with game_metadata(archive, member, args) as (game, info, status, detail):
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                rows = list(pool.map(lambda backend: run_one(backend, game, info, args), pending))
            for row in rows:
                if info is not None:
                    row.update(zip(GAME_FIELDS, info))
                elif not row.get("detail"):
                    row["detail"] = f"game metadata {status}: {detail}"
            return rows
    except NETWORK_ERRORS as error:
        return [dict(backend=backend.name, outcome="download failed", detail=str(error))
                for backend in pending]


def refresh_metadata(archive, member, rows, args):
    try:
        with game_metadata(archive, member, args) as (_, info, _, _):
            if info is None:
                return False
            for row in rows:
                row.update(zip(GAME_FIELDS, info))
                if row["detail"].startswith("game metadata "):
                    row["detail"] = ""
        return True
    except NETWORK_ERRORS as error:
        print(f"{member.filename}: metadata download failed: {error}", file=sys.stderr)
        return False


def publish(path, rows):
    try:
        retry(lambda: save_results(path, rows))
    except OSError as error:
        # Do not misclassify CSV storage failure as a failed archive directory.
        raise RuntimeError(f"cannot publish {path}; previous results preserved: {error}") from error


def run(args):
    rows = load_results(args.results)
    if args.backfill_build is not None:
        for row in rows:
            if not row["solver_build"]:
                row["solver_build"] = args.backfill_build
        publish(args.results, rows)
        return 0
    retry_games = {(row["archive"], row["game"]) for row in rows
                   if row["outcome"] in args.retry}
    refresh_games = {(row["archive"], row["game"]) for row in rows
                     if any(row[field] == "" for field in GAME_FIELDS) and row["game"] != "(archive listing)"
                     and args.games.search(row["game"])}
    machine = machine_info()
    base = dict(timeout_seconds=args.timeout, memory_gib=args.memory, jobs=args.jobs,
                max_host_leaves=args.max_host_leaves, machine=machine,
                solver_build=solver_build(args.build_dir) if not args.refresh_metadata else "")
    start, attempted = time.monotonic(), 0
    failed = False
    args.work_dir.mkdir(parents=True, exist_ok=True)
    references = {}
    if not args.refresh_metadata:
        try:
            references = reference_solutions(download_metadata(args.work_dir))
            print(f"metadata: {len(references)} original-game reference solutions", file=sys.stderr)
        except Exception as error:
            print(f"metadata unavailable: {error}", file=sys.stderr)
    for name in args.archives:
        if args.refresh_metadata and not any(archive == name for archive, _ in refresh_games):
            continue
        try:
            with remote_zip(ARCHIVES[name]) as archive:
                members = game_members(archive)
                if not members:
                    raise OSError("archive contains no games")
                indexed = [row for row in rows if not (row["archive"] == name and row["game"] == "(archive listing)")]
                if not args.refresh_metadata and len(indexed) != len(rows):
                    rows = indexed
                    publish(args.results, rows)
                for member in members:
                    game_name = member.filename.removesuffix(".bz2")
                    if not args.games.search(game_name):
                        continue
                    retrying = (name, game_name) in retry_games
                    if args.refresh_metadata:
                        if (name, game_name) not in refresh_games:
                            continue
                    else:
                        pending = args.backends if retrying else missing_backends(rows, name, game_name, args.backends)
                        if not pending:
                            continue
                    if args.max_games is not None and attempted >= args.max_games:
                        break
                    attempted += 1
                    if args.refresh_metadata:
                        saved = [row for row in rows if row["archive"] == name and row["game"] == game_name]
                        if refresh_metadata(archive, member, saved, args):
                            publish(args.results, rows)
                            print(f"{name}/{game_name}: metadata refreshed", file=sys.stderr, flush=True)
                        else:
                            failed = True
                        continue
                    common = dict(base, archive=name, game=game_name,
                                  reference_solution=reference_for_game(references, game_name))
                    new = measure_game(archive, member, pending, args)
                    for row in new:
                        row["detail"] = " ".join(str(row.get("detail", "")).split())[:240]
                    new = [dict(common, **row) for row in new]
                    replaced = {row["backend"] for row in new}
                    previous = [] if retrying else [row for row in rows if row["archive"] == name and row["game"] == game_name and row["backend"] not in replaced]
                    flag_winners(previous + new)
                    rows = [row for row in rows if not (row["archive"] == name and row["game"] == game_name)] + previous + new
                    # CSV storage failures must stop, keeping previously published games intact.
                    publish(args.results, rows)
                    counts = Counter(row["outcome"] for row in new)
                    print(f"{name}/{game_name}: " + ", ".join(f"{count} {outcome}" for outcome, count in counts.items()), file=sys.stderr, flush=True)
                    for row in new:
                        if row["outcome"] not in ("complete", "partial"):
                            print(f"  {row['backend']}: {row['outcome']}: {row.get('detail', '')}", file=sys.stderr)
        except NETWORK_ERRORS as error:
            if args.refresh_metadata:
                failed = True
                print(f"{name}: metadata archive listing failed: {error}", file=sys.stderr)
                continue
            # Without a directory there are no known game names; keep an explicit index failure.
            rows = [row for row in rows if not (row["archive"] == name and row["game"] == "(archive listing)")]
            rows.append(dict(base, archive=name, game="(archive listing)", backend="",
                             outcome="download failed", detail=str(error)[:240]))
            publish(args.results, rows)
            print(f"{name}: download failed (archive listing): {error}", file=sys.stderr)
        if args.max_games is not None and attempted >= args.max_games:
            break
    if not args.results.exists():
        publish(args.results, rows)
    print(f"{attempted} games attempted in {time.monotonic() - start:.1f} s; {len(rows)} saved rows", file=sys.stderr)
    return int(failed or any(row["outcome"] == "download failed" for row in rows))


def cell(value):
    return html.escape(str(value if value != "" and value is not None else "-"), quote=False).replace("|", "\\|").replace("\n", " ").replace("\r", " ")


def table(headers, rows):
    return ("| " + " | ".join(headers) + " |\n| " + " | ".join(["---"] * len(headers)) + " |\n"
            + "".join("| " + " | ".join(cell(v) for v in row) + " |\n" for row in rows) + "\n")


def pair(row, field):
    return " / ".join(str(row.get(f"{field}_{p}", ""))
                      if row.get(f"{field}_{p}", "") not in ("", None) else "-"
                      for p in ("even", "odd"))


def checks(rows, *, compact=False):
    groups = defaultdict(list)
    for row in rows:
        groups[row["archive"], row["game"]].append(row)
    problems = []
    for (archive, game), group in groups.items():
        flag_winners(group)
        disagree = [row for row in group if row.get("winner_disagrees") == "true"]
        if disagree:
            problems.append((archive, game, "winner counts or start winners disagree", "; ".join(
                row["backend"] + ": " + pair(row, "won") + ", start=" + (row.get("start_winner") or "unchecked")
                for row in disagree)))
    problems += [(row["archive"], row["game"], "reference mismatch",
                  f"{row['backend']}: reference={row['reference_solution']}, start={row['start_winner']}")
                 for row in rows if row.get("reference_agrees") == "false"]
    if compact:
        counts = Counter((archive, problem) for archive, _, problem, _ in problems)
        return "## Winner checks\n\n" + table(
            ["Archive", "Winner disagreements", "Reference mismatches"],
            [[archive, counts[archive, "winner counts or start winners disagree"],
              counts[archive, "reference mismatch"]]
             for archive in dict.fromkeys(row["archive"] for row in rows)]) + (
                 "Full lists of affected games and backends are on the archive pages linked below.\n\n")
    return "## Winner checks\n\n" + (table(["Archive", "Game", "Problem", "Details"], problems) if problems else "No disagreements in winner counts or start winners, and no recorded reference mismatches.\n\n")


def summary(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[row["archive"], row["backend"]].append(row)
    lines = ["## Summary\n\n", "Counts are backend runs. Archive-listing failures have no backend and do not count as games.\n\n"]
    counts, metrics = [], []
    for (archive, backend), group in groups.items():
        count = Counter(row["outcome"] for row in group)
        counts.append([archive, backend, len({r["game"] for r in group if r["backend"]}),
                       *(count[outcome] for outcome in OUTCOMES)])
        complete = [row for row in group if row["outcome"] == "complete"]
        values = [archive, backend, len(complete)]
        for metric in ("nodes", "leaves", "height", "width", "strahler"):
            for player in ("even", "odd"):
                samples = [float(row[f"{metric}_{player}"]) for row in complete if row.get(f"{metric}_{player}", "") != ""]
                values.append(f"{median(samples):g} / {max(samples):g}" if samples else "-")
        metrics.append(values)
    if not counts:
        return "## Summary\n\nNo benchmark runs recorded.\n\n"
    lines.append(table(["Archive", "Backend", "Games", *OUTCOMES], counts))
    lines.append("Tree statistics use complete runs only, including zero-sized trees. Each cell is median / maximum; E and O are separate columns.\n\n")
    lines.append(table(["Archive", "Backend", "Complete", *(f"{m} {p}" for m in ("nodes", "leaves", "height", "width", "strahler") for p in ("E", "O"))], metrics))
    return "".join(lines)


def inventory(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[row["game"]].append(row)
    sections = []
    for game, group in groups.items():
        first = group[0]
        flag_winners(group)
        disagreements = [row for row in group if row.get("winner_disagrees") == "true"]
        mismatches = [row["backend"] for row in group if row.get("reference_agrees") == "false"]
        counts = Counter(row["outcome"] for row in group)
        outcomes = " · ".join(f"{counts[outcome]} {outcome}" for outcome in OUTCOMES if counts[outcome])
        markers = (" · WINNER DISAGREEMENT" if disagreements else "") + (" · REFERENCE MISMATCH" if mismatches else "")
        lines = [f"<details>\n<summary>{cell(game)} · {cell(first.get('vertices', ''))} vertices / "
                 f"{cell(first.get('edges', ''))} edges · {outcomes}{markers}</summary>\n\n"]
        if disagreements:
            lines.append("Winner counts or start winners disagree: " + "; ".join(
                cell(row["backend"]) + ": " + pair(row, "won") + ", start=" + (row.get("start_winner") or "unchecked")
                for row in disagreements) + ".\n\n")
        if mismatches:
            lines.append("Reference mismatches: " + ", ".join(map(cell, mismatches)) + ".\n\n")
        lines.append(table(["Vertices", "Edges", "Distinct priorities", "Maximum priority"], [[first.get(key, "") for key in GAME_FIELDS]]))
        lines.append(table(["Backend", "Outcome", "Won E / O", "Nodes E / O", "Leaves E / O", "Height E / O", "Parameters E / O", "Seconds", "Peak RSS MiB", "Start winner", "Reference", "Agrees", "Winner disagreement", "Detail"],
            [[row["backend"], row["outcome"], *(pair(row, key) for key in ("won", "nodes", "leaves", "height", "parameters")), row.get("seconds", ""), row.get("peak_rss_mib", ""), row.get("start_winner", ""), row.get("reference_solution", ""), row.get("reference_agrees", ""), row.get("winner_disagrees", ""), row.get("detail", "")] for row in group]))
        remaining = [metric for metric in decomp.TREE_METRICS if metric not in ("nodes", "leaves", "height")]
        if any(row.get("nodes_even", "") != "" for row in group):
            lines.append(table(["Backend", *(metric + " E / O" for metric in remaining)], [[row["backend"], *(pair(row, metric) for metric in remaining)] for row in group]))
        lines.append("</details>\n\n")
        sections.append((group, "".join(lines)))
    return sections


def dataset_notes(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[row["archive"], row["game"]].append(row)
    notes = []
    builds = defaultdict(list)
    for row in rows:
        builds[row.get("solver_build", "")].append(row)
    if builds:
        notes.append("Recorded solver builds (blank provenance is unrecorded):\n\n" + table(
            ["Solver build", "Rows", "Games"],
            [[build or "(unrecorded)", len(group),
              len({(row["archive"], row["game"]) for row in group if row["game"] != "(archive listing)"})]
             for build, group in sorted(builds.items())]))
    if len(builds) > 1:
        majority = max(map(len, builds.values()))
        rerun = sorted({(row["archive"], row["game"]) for build, group in builds.items()
                        if build and len(group) < majority for row in group
                        if row["game"] != "(archive listing)"})
        if rerun:
            notes.append("Games with rows from a non-majority build were rerun after earlier rows "
                         "reported the parser allocation failure `failed to allocate the input buffer`:\n\n"
                         + "".join(f"- {cell(archive)}: <code>{cell(game)}</code>\n" for archive, game in rerun) + "\n")
    invalid = [game for (_, game), group in groups.items()
               if any(row["outcome"] == "invalid input" for row in group)]
    if invalid:
        notes.append(f"{len(invalid)} games are invalid PGSolver input. Each ends with a stray "
                     "`timeout: WARNING: Wait(N) failed: No child processes` line leaked from the generator:\n\n"
                     + "".join(f"- `{game}`\n" for game in invalid) + "\n")
    if any(PurePosixPath(row["game"]).name in ("FLCTLStarLimitClosuren=1.gm", "FLCTLStarLimitClosuren=1_compact.gm") and
           row.get("reference_agrees") == "false" for row in rows):
        notes.append("`FLCTLStarLimitClosure n=1` (plain and compact) has a likely error in the reference data. "
                     "Every complete backend and upstream [Oink](https://github.com/trolando/oink) "
                     "(`--zlk`) agree that Even wins start vertex 0, with identical Even / Odd winner counts: "
                     "8,276 / 1,309 for the compact game and 148,284 / 26,383 for the plain game. "
                     "`data.yaml` records pbespgsolve's answer as false for all reductions.\n\n")
    refused = {key: group for key, group in groups.items() if key[0] == "pgsolver"
               and any("+pre" not in row["backend"] and "safe recursion depth" in row.get("detail", "")
                       for row in group)}
    if refused:
        priorities = [int(row["distinct_priorities"]) for group in refused.values()
                      for row in group if row.get("distinct_priorities") not in (None, "")]
        # Old +pre details lost the error line; their recorded error outcome survives.
        remaining = [game for (_, game), group in refused.items()
                     if any(row["backend"].startswith(("lifting", "zielonka")) and
                            "+pre" in row["backend"] and row["outcome"] == "error" for row in group)]
        passed = sum(any(row["backend"].startswith(("lifting", "zielonka")) and
                         "+pre" in row["backend"] and row["outcome"] in ("complete", "partial")
                         for row in group) for group in refused.values())
        families = ", ".join(sorted({PurePosixPath(game).name.split("(")[0] for _, game in refused}))
        remaining_families = Counter(PurePosixPath(game).name.split("(")[0] for game in remaining)
        notes.append(f"{len(refused)} pgsolver games ({families}) exceed the recursion depth limit of 1024 "
                     "in `zielonka`, `zielonka-sym`, `lifting`, `lifting-sym` and `lifting-adaptive`, "
                     "including `+iter`. "
                     + (f"They have between {min(priorities):,} and {max(priorities):,} distinct priorities. "
                        if priorities else "")
                     + f"With `+pre`, compression gets {passed} games past the guard."
                     + (" The remaining " + " and ".join(f"{count} {family}" for family, count in sorted(remaining_families.items()))
                        + " games stay refused because alternating parities leave nothing to merge." if remaining else "")
                     + "\n\n")
    buffer_oom = [row for row in rows if "failed to allocate the input buffer" in row.get("detail", "")]
    if buffer_oom:
        count = len({(row["archive"], row["game"]) for row in buffer_oom})
        notes.append(f"{count} of the largest games ({len(buffer_oom)} backend runs) fail before parsing: "
                     "the tools read the whole file into memory and report "
                     "`failed to allocate the input buffer` (out of memory).\n\n")
    downloads = {row["game"] for row in rows if row["archive"] == "equivchecking"
                 and row["outcome"] == "download failed"}
    if downloads:
        notes.append(f"The macOS `equivchecking.zip` has broken ZIP64 offsets; {len(downloads)} games "
                     "still marked `download failed` need rerunning with local-header offset recovery.\n\n")
    return "## Dataset notes\n\n" + "".join(notes) if notes else ""


def intro(rows):
    budgets = sorted({(row.get("timeout_seconds", ""), row.get("memory_gib", ""), row.get("jobs", ""), row.get("max_host_leaves", ""), row.get("machine", "")) for row in rows})
    measured = any(row["backend"] for row in rows)
    return "# Keiren benchmark metrics\n\n" + SOURCE + (
        ("" if measured else "No benchmark games have been measured.\n\n")
        +
        "These are static measurements. Games and tree output are removed after each game; "
        "only the CSV and this report are kept. CI does not download or rerun this dataset.\n\n"
        "The default budget is 30 seconds per game and backend, including solver output parsing, "
        "with a 2 GiB address-space cap per process and three concurrent backends. "
        "The intended full-run machine is Linux with 4 cores and 7 GB RAM. "
        "macOS does not enforce RLIMIT_AS. The default host-leaf limit is `SIZE_MAX` "
        "(18,446,744,073,709,551,615 on 64-bit hosts), the largest value pg2adot accepts, "
        "so time and memory budgets govern the run. DECOMP keeps its 10,000-leaf default. "
        "Adaptive comparison runs and progress-measure leaf reruns are omitted.\n\n"
        + (table(["Timeout (s)", "Memory (GiB)", "Jobs", "Host leaves", "Recorded machine"], budgets) if budgets else "No execution machine has been recorded.\n\n")
        + "`reference_solution` preserves the original-game Boolean when an unambiguous file match "
        "exists in `data.yaml`. In the [generator's cases/__init__.py]"
        "(https://github.com/jkeiren/paritygame-generator/blob/master/cases/__init__.py), "
        "pgsolver's true means Even wins vertex 0; pbespgsolve's true means Even wins the initial vertex. "
        "These games declare `start 0;`. We compare the reference with `start_winner` for the declared "
        "`start N;`, defaulting to vertex 0 when omitted: true means Even wins, false means Odd wins. "
        "`reference_agrees` is blank if the reference or a certified start winner is unavailable. "
        "Older pilot rows lack `start_winner` and remain unchecked. Winner cross-checks compare "
        "complete backends' Even/Odd counts and start winners; different winning sets of equal size "
        "with the same start winner can still go undetected.\n\n"
        + dataset_notes(rows)
        + "## Columns and metrics\n\n"
        + table(["Column", "Meaning"], [
            ["archive, game", "Source ZIP name and member path, with the .bz2 suffix removed. (archive listing) records a failed ZIP directory download."],
            ["backend", "DECOMP solver name. +pre compresses priorities and repeatedly removes simple winning regions and their attractors; +iter removes certified dominions and their attractors between solver rounds."],
            ["outcome", "complete assigns every vertex; partial leaves vertices unresolved; host limit exceeds the allowed host-tree size; timeout exceeds the deadline; out of memory reports allocation failure or a signal with evidence of the cap; crash records another signal; error records another exit or parsing failure; invalid input is rejected PGSolver input; download failed exhausts network/disk retries."],
            ["vertices, edges", "Game vertices (final declarations, including replacements) and successor entries."],
            ["distinct_priorities, maximum_priority", "Number of distinct input priorities and the greatest input priority, before compression."],
            ["won_even, won_odd (Won E / O)", "Certified winning vertices, including all shaving rounds and attracted vertices, once each."],
            ["parameters_even, parameters_odd", decomp.PARAMETERS_GUIDE],
            ["seconds", "Wall seconds for a backend, including launch, solver execution and metric parsing. Downloading and shared game-metadata parsing are excluded."],
            ["peak_rss_mib", "Maximum individual child peak RSS, measured with os.wait4 for the solver and metric parser; MiB is 2^20 bytes. It is not a cumulative RUSAGE_CHILDREN reading."],
            ["detail", "Short error, signal or unavailable-metadata explanation. Older non-complete +pre rows with only preprocessing statistics show (error text not recorded); the CSV is unchanged."],
            ["start_winner", "even or odd for the declared start vertex (default 0), including shaved winners; unresolved if uncertified or the vertex is absent. Blank in older pilot rows."],
            ["reference_solution, reference_agrees", "Original reference Boolean and true/false agreement with a certified start winner; blank means unchecked."],
            ["winner_disagrees", "true marks complete rows when complete backends disagree on winning counts or known start winners; false means no disagreement found; blank means incomplete."],
            ["timeout_seconds, memory_gib, jobs, max_host_leaves, machine", "Recorded execution limits and machine for each row; memory_gib is in 2^30 bytes."],
            ["solver_build", "First line of pg2adot --version, with Meson build type and optimization when available. Empty for older rows until explicitly backfilled."],
        ])
        + "Paired values use Even / Odd (E / O); CSV columns use _even and _odd. "
        "A hyphen in the tables or an empty CSV field means unavailable. "
        "Tree metrics cover the remaining game (after preprocessing and earlier shaving rounds): "
        "its decomposition or occupied progress-measure tree, "
        "including its extra root. Empty trees have zero metrics. Connector boxes, shaved "
        "regions and unresolved vertices do not count. Parameters and trees describe the "
        "final round; winning counts include all rounds.\n\n"
        + decomp.TREE_METRIC_GUIDE
    )


def clean_detail_pages(output, keep):
    for path in (output.parent / "keiren").glob("*-*.md"):
        name, number = path.stem.rsplit("-", 1)
        if name in ARCHIVES and number.isdecimal() and path not in keep:
            path.unlink()


def render(results, output, page_limit=300_000):
    rows = load_results(results, allow_legacy=True)
    by_archive = defaultdict(list)
    for row in rows:
        if ("+pre" in row["backend"] and row["outcome"] != "complete"
                and re.match(r"(?:exit \d+: )?preprocess: residual=", row["detail"])):
            row["detail"] = "(error text not recorded)"
        by_archive[row["archive"]].append(row)
    introduction, totals = intro(rows), summary(rows)
    sections = {name: inventory(group) for name, group in by_archive.items()}
    full = introduction + checks(rows) + totals + "## Inventory\n\n" + "".join(
        f"## {name}\n\n" + "".join(part for _, part in parts)
        for name, parts in sections.items())
    output.parent.mkdir(parents=True, exist_ok=True)
    if len(full.encode("utf-8")) <= page_limit:
        output.write_text(full, encoding="utf-8")
        clean_detail_pages(output, set())
        return
    # Split at game boundaries, then paginate archives that exceed GitHub's limit too.
    directory = output.parent / "keiren"
    directory.mkdir(parents=True, exist_ok=True)
    links, written = [], set()
    for name, parts in sections.items():
        def page(number, group, content):
            return (f"# {name}, part {number}\n\n[Definitions and summary](../{output.name}).\n\n"
                    + checks(group) + "## Inventory\n\n" + content)
        chunks, chunk = [], ""
        chunk_rows = []
        for group, part in parts:
            if chunk and len(page(len(chunks) + 1, chunk_rows + group, chunk + part).encode("utf-8")) > page_limit:
                chunks.append(page(len(chunks) + 1, chunk_rows, chunk))
                chunk_rows, chunk = [], ""
            chunk_rows += group
            chunk += part
            if len(page(len(chunks) + 1, chunk_rows, chunk).encode("utf-8")) > page_limit:
                raise ValueError(f"one game's report exceeds the page limit: {group[0]['game']}")
        if chunk:
            chunks.append(page(len(chunks) + 1, chunk_rows, chunk))
        for number, content in enumerate(chunks, 1):
            path = directory / f"{name}-{number}.md"
            path.write_text(content, encoding="utf-8")
            written.add(path)
            links.append(f"- [{name}, part {number}](keiren/{path.name})\n")
    output.write_text(introduction + checks(rows, compact=True) + totals
                      + "## Inventory\n\n" + "".join(links), encoding="utf-8")
    clean_detail_pages(output, written)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    runner = commands.add_parser("run", help="download and run selected games; resume existing CSV")
    runner.add_argument("--archives", default=",".join(ARCHIVES), help="comma-separated ZIP names without .zip")
    runner.add_argument("--games", default="", metavar="REGEX", help="re.search filter on the saved game path, without .bz2")
    runner.add_argument("--backends", default=",".join(b.name for b in decomp.BACKENDS))
    mode = runner.add_mutually_exclusive_group()
    mode.add_argument("--retry", default="", help="comma-separated outcomes; replace each matching game's rows after rerunning selected backends")
    mode.add_argument("--refresh-metadata", action="store_true", help="fill missing game metadata in saved rows without running backends")
    mode.add_argument("--backfill-build", metavar="TEXT", help="fill only empty solver_build cells atomically; no downloads or solver runs")
    runner.add_argument("--max-games", type=int, help="maximum newly attempted games, across selected archives")
    runner.add_argument("--work-dir", type=Path, default=Path(tempfile.gettempdir()) / "strahler-keiren")
    runner.add_argument("--build-dir", type=Path, default=Path("build"))
    runner.add_argument("--timeout", type=float, default=30)
    runner.add_argument("--metadata-timeout", type=float, default=600,
                        help="seconds for input metadata parsing (default: 600)")
    runner.add_argument("--memory", type=float, default=2, help="per-process address space in GiB (default: 2)")
    runner.add_argument("--jobs", type=int, default=3)
    runner.add_argument("--max-host-leaves", type=int, default=MAX_HOST_LEAVES)
    for command in (runner, commands.add_parser("render", help="read only saved CSV; no downloads or solvers")):
        command.add_argument("--results", type=Path, default=decomp.ROOT / "keiren/results.csv")
    commands.choices["render"].add_argument("--output", type=Path, default=decomp.ROOT / "KEIREN.md")
    args = parser.parse_args()
    if args.command == "render":
        render(args.results, args.output)
        return 0
    if args.backfill_build is not None and not args.backfill_build.strip():
        parser.error("--backfill-build must not be empty")
    try:
        args.games = re.compile(args.games)
    except re.error as error:
        parser.error(f"invalid --games regex: {error}")
    args.archives = list(dict.fromkeys(args.archives.split(",")))
    args.retry = [outcome.strip() for outcome in args.retry.split(",")] if args.retry else []
    if any(outcome not in OUTCOMES for outcome in args.retry):
        parser.error("unknown retry outcome; choose from " + ", ".join(OUTCOMES))
    valid = {backend.name: backend for backend in decomp.BACKENDS}
    names = list(dict.fromkeys(args.backends.split(",")))
    if any(name not in ARCHIVES for name in args.archives):
        parser.error("unknown archive; choose from " + ", ".join(ARCHIVES))
    if any(name not in valid for name in names):
        parser.error("unknown backend; choose from " + ", ".join(valid))
    args.backends = [valid[name] for name in names]
    if not math.isfinite(args.memory) or args.memory <= 0:
        parser.error("--memory must be finite and positive")
    if not math.isfinite(args.metadata_timeout) or args.metadata_timeout <= 0:
        parser.error("--metadata-timeout must be finite and positive")
    if args.jobs <= 0 or (args.max_games is not None and args.max_games <= 0):
        parser.error("--jobs and --max-games must be positive")
    args.build_dir = decomp.validate_run_arguments(parser, args,
        [] if args.refresh_metadata or args.backfill_build is not None else args.backends)
    args.work_dir = args.work_dir.resolve()
    if args.work_dir.is_relative_to(decomp.ROOT):
        parser.error("--work-dir must be outside the repository")
    if sys.platform == "darwin":
        print("macOS does not enforce RLIMIT_AS", file=sys.stderr)
    args.results.parent.mkdir(parents=True, exist_ok=True)
    with args.results.with_suffix(".lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error("another runner is already writing this CSV")
        return run(args)


def internal_command():
    if sys.argv[1] == "_exec":
        cap = int(sys.argv[2])
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        if sys.platform != "darwin":
            resource.setrlimit(resource.RLIMIT_AS, (cap, cap))
        os.execvpe(sys.argv[3], sys.argv[3:], os.environ)
    elif sys.argv[1] == "_game_info":
        print(json.dumps(decomp.game_info(Path(sys.argv[2]))))
    elif sys.argv[1] == "_metrics":
        name, out, err, metadata = sys.argv[2:]
        backend = next(backend for backend in decomp.BACKENDS if backend.name == name)
        stats = Path(err).read_text(encoding="utf-8", errors="replace")
        outcomes = re.findall(r"\bresult=(complete|partial)\b", stats)
        info = json.loads(metadata)
        run = decomp.Run(outcomes[-1], stats, Path(out).read_text(encoding="utf-8", errors="replace"))
        columns = decomp.metric_columns(decomp.run_metrics(backend, run, info[0] if info is not None else None))
        start = re.search(r"^start: vertex=\d+ winner=(even|odd|unresolved)$", stats, re.MULTILINE)
        if start is None:
            raise ValueError("missing start statistics; rebuild pg2adot and pg2pmdot")
        columns["start_winner"] = start[1]
        print(json.dumps(columns))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("_exec", "_game_info", "_metrics"):
        try:
            internal_command()
        except Exception as error:
            print(f"{type(error).__name__}: {error}", file=sys.stderr)
            raise SystemExit(1)
    else:
        try:
            raise SystemExit(main())
        except (OSError, ValueError, RuntimeError) as error:
            print(f"gen_keiren: {error}", file=sys.stderr)
            raise SystemExit(1)
