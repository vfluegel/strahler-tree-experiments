#!/usr/bin/env python3
"""Compare Oink winners and SSPM measures (differences in tests/README.md)."""

import argparse
from collections import Counter
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
STREAM = re.compile(r"(?:[01e,]*\|)+")


def run(command, timeout):
    result = subprocess.run(command, capture_output=True, text=True,
                            timeout=timeout, check=False)
    if result.returncode:
        raise ValueError(f"{command}: exit {result.returncode}\n"
                         f"{result.stdout}\n{result.stderr}")
    return result.stdout


def branches(stream):
    """Keep multiplicities, including identical printed internal measures."""
    if stream and not STREAM.fullmatch(stream):
        raise ValueError(f"invalid leaf stream: {stream!r}")
    return Counter(stream.split("|")[:-1])


def oracle_stream(log):
    lines = [re.sub(r"^\[\s*[0-9.]+\]\s?", "", line)
             for line in log.splitlines()]
    streams = [line for line in lines if STREAM.fullmatch(line)]
    if not streams:
        raise ValueError(f"Oink printed no measure stream:\n{log}")
    return streams[-1]


def our_stream(output):
    """Select the last round's last-run winner (Oink names counter players)."""
    # Odd counters solve Even first. An empty Odd stream means Even finished.
    streams = re.findall(r"^(even|odd): (.*)$", output, re.MULTILINE)
    if len(streams) < 2 or streams[-2][0] != "even":
        raise ValueError(f"missing round/player streams:\n{output}")
    return streams[-1][1] or streams[-2][1]


def solution(text, count):
    winners = {}
    for vertex, player in re.findall(r"^(\d+) ([01])(?: \d+)?;\s*$",
                                     text, re.MULTILINE):
        vertex = int(vertex)
        if vertex in winners:
            raise ValueError(f"duplicate solution vertex {vertex}")
        winners[vertex] = int(player)
    header = re.search(r"^paritysol (\d+);$", text, re.MULTILINE)
    if (header is None or int(header[1]) != len(winners)
            or not set(winners) <= set(range(count))):
        raise ValueError("invalid Oink solution")
    return winners


def dot_winners(output, count):
    won = [set(), set()]
    player = None
    unresolved = set()
    for line in output.splitlines():
        if line.startswith("digraph "):
            unresolved = set()
        match = re.search(r'label="(Even|Odd) \(', line)
        if match:
            player = int(match[1] == "Odd")
        for kind, members in re.findall(r"\\n([VWU])=\{([0-9,]*)\}", line):
            vertices = {int(vertex) for vertex in members.split(",") if vertex}
            if kind == "U":
                unresolved = vertices
            elif player is None:
                raise ValueError("winner set outside a player tree")
            else:
                won[player].update(vertices)
    if (won[0] & won[1] or unresolved & (won[0] | won[1])
            or won[0] | won[1] | unresolved != set(range(count))):
        raise ValueError("our regions do not partition the game")
    return {vertex: player for player in range(2) for vertex in won[player]}


def random_game(rng):
    count = rng.randint(2, 32)
    rows = []
    for vertex in range(count):
        priority = rng.randrange(16)
        owner = rng.randrange(2)
        edges = rng.sample(range(count), rng.randint(1, min(count, 4)))
        rows.append(f"{vertex} {priority} {owner} "
                    + ",".join(map(str, edges)) + ";")
    return f"parity {count - 1};\n" + "\n".join(rows) + "\n"


def canonical_game(game):
    """Match vertex and predecessor order after Oink's reindexing."""
    rows = re.findall(r'^\s*(\d+)\s+(\d+)\s+([01])\s+([\d,\s]+)'
                      r'(?:".*")?\s*;', game, re.MULTILINE)
    rows = [(int(v), int(p), int(o), edges) for v, p, o, edges in rows]
    rows.sort(key=lambda row: (row[1], row[0]))
    ids = {row[0]: index for index, row in enumerate(rows)}
    if not rows or len(ids) != len(rows):
        raise ValueError("invalid or empty comparison game")
    return f"parity {len(rows) - 1};\n" + "".join(
        f"{ids[v]} {p} {o} "
        + ",".join(str(ids[int(edge)]) for edge in edges.split(",")) + ";\n"
        for v, p, o, edges in rows)


def compare(args, game, algorithm, scratch):
    count = len(re.findall(r"^\s*\d+\s+\d+\s+[01]\s+", game,
                           re.MULTILINE))
    path = scratch / "game.pg"
    path.write_text(game)
    sol = scratch / "game.sol"
    flag = "--strpm" if algorithm == "strahler" else "--sspm"
    ours = [args.pg2pmdot, f"--algorithm={algorithm}"]
    if algorithm == "succinct":
        # --sspm is unbounded; our default schedule corresponds to --bsspm.
        ours.append(f"--bits={(count - 1).bit_length()}")
    failures = []
    transcripts = []
    streams = 0
    for shave in (False, True):
        sol.unlink(missing_ok=True)
        command = [args.oink, flag, "--output", str(sol)]
        if not shave:
            command += ["--no-single", "--no-loops", "--no-wcwc"]
        log = run(command + [str(path)], args.timeout)
        expected = solution(sol.read_text(), count)
        options = ["--initial-shave=all"] if shave else []
        mode = [] if shave else ["--iter-shave"]
        dot = run(ours + mode + options + ["--labels=sets",
                  f"--max-set-items={count}", str(path)], args.timeout)
        transcripts += [f"shave={shave}\n{log}\n{dot}"]
        actual_winners = dot_winners(dot, count)
        if algorithm == "strahler":
            # Upstream retains nonstrict tails; the corrected least lift can
            # certify more vertices or stop at an earlier parameter pair.
            # Check partial certificates against an independent full solve.
            reference = expected
            if len(expected) != count or len(actual_winners) != count:
                sol.unlink()
                log = run([args.oink, "--zlk", "--output", str(sol), str(path)],
                          args.timeout)
                transcripts.append("Zielonka reference:\n" + log)
                reference = solution(sol.read_text(), count)
            if len(reference) != count:
                failures.append("incomplete Zielonka reference")
            for name, partial in (("Oink", expected), ("native", actual_winners)):
                if any(reference.get(v) != p for v, p in partial.items()):
                    failures.append(f"unsound {name} Strahler solution")
        elif actual_winners != expected:
            failures.append("shaved winners" if shave else "winners")
        if not shave and algorithm == "succinct":
            leaves = run(ours + ["--iter-shave", "--format=leaves",
                                 str(path)],
                         args.timeout)
            transcripts.append(leaves)
            actual = our_stream(leaves)
            oracle = oracle_stream(log)
            streams += 1
            if branches(actual) != branches(oracle):
                failures.append(f"stream: Oink={oracle!r} ours={actual!r}")
            selected = re.search(r"max l (\d+)\.", log)
            rounds = re.findall(r"# round \d+ bits=(\d+)", leaves)
            if (selected is None or not rounds
                    or selected[1] != rounds[-1]):
                failures.append("final succinct budget")
    return failures, "\n".join(transcripts), streams


def self_check():
    assert branches("e|0|e|") == Counter({"e": 2, "0": 1})
    assert branches("e|e|") != branches("e|")
    assert oracle_stream("[    0.00] Solved\n[    0.00] 0|00|\n") == "0|00|"
    assert our_stream("# round 2\neven: 0|00|\nodd: \n") == "0|00|"
    assert our_stream("even: 0|\nodd: e|\n") == "e|"
    assert solution("paritysol 2;\n0 0 1;\n1 1;\n", 2) == {0: 0, 1: 1}
    assert solution("paritysol 0;\n", 2) == {}
    assert canonical_game('parity 1;\n0 2 0 1 "x";\n1 0 1 0;\n') == (
        'parity 1;\n0 0 1 1;\n1 2 0 0;\n')
    dot = ('label="Even (complete)\\nV={0}";\n'
           'label="Odd (complete)\\nV={}\\nW={1}";\n')
    assert dot_winners(dot, 2) == {0: 0, 1: 1}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pg2pmdot", help="path to our built executable")
    parser.add_argument("--oink", default=os.environ.get("OINK_BIN"))
    parser.add_argument("--games", type=int, default=500)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=3.0,
                        help="per-command seconds; slow examples are skipped")
    parser.add_argument("--failures-dir", type=Path,
                        default=Path("build/oink-failures"))
    args = parser.parse_args()
    self_check()
    if not args.oink:
        print("SKIP: set OINK_BIN to a print-pms Oink binary")
        return 77
    if args.games < 1 or args.timeout <= 0:
        parser.error("--games and --timeout must be positive")
    for name in ("oink", "pg2pmdot"):
        binary = shutil.which(getattr(args, name))
        if binary is None:
            parser.error(f"{name} executable not found: {getattr(args, name)}")
        setattr(args, name, binary)
    rng = random.Random(args.seed)
    games = [(f"seed-{args.seed}-{index}", random_game(rng), False)
             for index in range(args.games)]
    games += [(path.name, path.read_text(), True)
              for path in sorted((ROOT / "examples").glob("*.pg"))]
    games += [(name, (ROOT / "tests/games" / name).read_text(), False)
              for name in ("pm_cycle.pg", "strahler_two.pg",
                           "split_winners.pg", "oink_search_limit.pg",
                           "oink_disabled.pg")]
    total = 0
    with tempfile.TemporaryDirectory(prefix="compare-oink-") as directory:
        for algorithm in ("strahler", "succinct"):
            checked = mismatches = skipped = 0
            streams = 0
            for name, game, example in games:
                compared = 0
                try:
                    game = canonical_game(game)
                    failures, log, compared = compare(
                        args, game, algorithm, Path(directory))
                except subprocess.TimeoutExpired as error:
                    if example:
                        skipped += 1
                        print(f"SKIP {algorithm} {name}: timeout", flush=True)
                        continue
                    failures, log = ["timeout"], str(error)
                except (ValueError, OSError) as error:
                    failures, log = ["execution/parser error"], str(error)
                checked += 1
                streams += compared
                if failures:
                    mismatches += 1
                    # Save every reproducer, but keep CI output readable.
                    args.failures_dir.mkdir(parents=True, exist_ok=True)
                    stem = args.failures_dir / f"{algorithm}-{name}"
                    Path(f"{stem}.pg").write_text(game)
                    Path(f"{stem}.log").write_text(
                        "\n".join(failures) + "\n" + log)
                    if mismatches <= 5:
                        print(f"FAIL {algorithm} {name}: "
                              + "; ".join(failures), flush=True)
            total += mismatches
            print(f"{algorithm}: {checked} games, {mismatches} mismatches, "
                  f"{streams} exact streams compared, "
                  f"{skipped} slow examples skipped (seed={args.seed})",
                  flush=True)
    if total:
        print(f"Reproducers and logs: {args.failures_dir.resolve()}")
    return int(total != 0)


if __name__ == "__main__":
    sys.exit(main())
