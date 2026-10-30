#!/usr/bin/env python3
"""Regenerate decomp/ and DECOMP.md from the built, verified game tools."""

import argparse
from array import array
from collections import Counter
from dataclasses import dataclass
from hashlib import sha256
from itertools import combinations
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zlib


ROOT = Path(__file__).resolve().parents[1]
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_HASH_CHUNK = b"tEXtdot-sha256\0"
TREE_METRICS = (
    "nodes", "leaves", "internal_nodes", "height", "width", "max_arity",
    "mean_internal_arity", "branching_nodes", "min_leaf_depth",
    "mean_leaf_depth", "strahler",
)


PARAMETERS_GUIDE = (
    "Final search bounds: lifting uses `H` (host height), `K` (Strahler bound), "
    "`t` (size allowance); `pm-strahler` uses `k` (Strahler bound), "
    "`t` (size allowance), `depth` (number of measure levels); "
    "`pm-succinct` uses `bits` (bit budget per measure) and `depth`. "
    "Zielonka methods have no search parameters."
)

TREE_METRIC_GUIDE = (
    "Depth counts nodes from the root (depth 1); means have three decimal "
    "places.\n\n"
    "- `nodes`: Number of nodes in the result tree. Several game vertices "
    "can belong to one tree node.\n"
    "- `leaves`: Number of tree nodes with no children.\n"
    "- `internal_nodes`: Number of tree nodes with at least one child.\n"
    "- `height`: Number of nodes on the longest path from the root to a leaf.\n"
    "- `width`: Largest number of nodes on one level.\n"
    "- `max_arity`: Largest number of children of one node.\n"
    "- `mean_internal_arity`: Mean number of children over internal nodes; "
    "zero if there are none.\n"
    "- `branching_nodes`: Number of tree nodes with at least two children.\n"
    "- `min_leaf_depth`: Fewest nodes on a path from the root to a leaf.\n"
    "- `mean_leaf_depth`: Mean number of nodes on a path from the root to a leaf, "
    "over all leaves.\n"
    "- `strahler`: A leaf has value 1. At an internal node, take the largest "
    "child value and add 1 if at least two children share that largest value. "
    "The tree's value is the root's value.\n\n"
    "Each player's Strahler number is the smallest Strahler number among that\n"
    "player's attractor decompositions; the game's Strahler number is the larger of\n"
    "the two. For a complete result without shaving, the larger of the reported Even\n"
    "and Odd values is an upper bound for the game's.\n"
    "With shaving or partial results, the metrics describe only the represented\n"
    "part of the remaining game.\n\n"
)


@dataclass(frozen=True)
class Backend:
    name: str
    binary: str
    options: tuple[str, ...]


BASE_BACKENDS = (
    Backend("lifting", "pg2adot", ("--algorithm=lifting",)),
    Backend("lifting-adaptive", "pg2adot",
            ("--algorithm=lifting", "--adaptive-t")),
    Backend("lifting-sym", "pg2adot", ("--algorithm=lifting-sym",)),
    Backend("pm-strahler", "pg2pmdot", ("--algorithm=strahler",)),
    Backend("pm-succinct", "pg2pmdot", ("--algorithm=succinct",)),
    Backend("zielonka", "pg2adot", ("--algorithm=zielonka",)),
    Backend("zielonka-sym", "pg2adot", ("--algorithm=zielonka-sym",)),
)
BACKENDS = tuple(
    Backend(backend.name + ("+pre" if pre else "") +
            ("+iter" if iterative else ""), backend.binary,
            backend.options + (("--initial-shave=all", "--compress") if pre else ())
            + (("--iter-shave",) if iterative else ()))
    for backend in BASE_BACKENDS
    for iterative in ((False,) if backend.name.startswith("zielonka")
                      else (False, True))
    for pre in (False, True)
)
MAX_SET_ITEMS = 8
# Per-game label overrides belong here if a DOT approaches 2 MB.
GAME_LABELS = {}
# Local release runs exceeded 60 s for each pair, with and without preprocessing.
SKIP_RUNS = {
    ("cabp_swp.ehoa.pg", "lifting-adaptive"): 60,
    ("cabp_swp.ehoa.pg", "lifting-adaptive+pre"): 60,
    ("cabp_swp.ehoa.pg", "lifting-adaptive+iter"): 60,
    ("cabp_swp.ehoa.pg", "lifting-adaptive+pre+iter"): 60,
    ("cabp_swp.ehoa.pg", "pm-strahler"): 60,
    ("cabp_swp.ehoa.pg", "pm-strahler+pre"): 60,
    ("cabp_swp.ehoa.pg", "pm-strahler+iter"): 60,
    ("cabp_swp.ehoa.pg", "pm-strahler+pre+iter"): 60,
}
HEADER = re.compile(r"parity\s+\d+\s*;")
VERTEX = re.compile(
    r'(\d+)\s+(\d+)\s+[01]\s+(\d+(?:\s*,\s*\d+)*)\s*'
    r'(?:"[^"]*"\s*)?;'
)
GAME_RECORD = re.compile(r"\s*(?:" + VERTEX.pattern + r"|(?P<directive>parity|start)\s+\d+\s*;)")


def discover_games(paths):
    """Recognize games by their first nonempty line, whatever the suffix."""
    games = []
    candidates = set()
    for path in paths:
        if not path.exists():
            raise ValueError(f"input does not exist: {path}")
        candidates.update(path.rglob("*") if path.is_dir() else [path])
    for path in sorted(candidates):
        if path.is_file():
            with path.open(encoding="utf-8", errors="replace") as stream:
                first = next((line.strip() for line in stream
                              if line.strip()), "")
            if HEADER.fullmatch(first):
                games.append(path)
    stems = [path.stem for path in games]
    if len(stems) != len(set(stems)):
        raise ValueError("games must have unique stems for output directories")
    return games


def game_records(stream, chunk_size):
    buffer = ""
    while chunk := stream.read(chunk_size):
        buffer += chunk
        end = 0
        while match := GAME_RECORD.match(buffer, end):
            yield match
            end = match.end()
        buffer = buffer[end:].lstrip()
        # Reject complete malformed records without retaining the rest of the file.
        if re.match(r'[^;"]*(?:"[^"]*"[^;"]*)*;', buffer):
            raise ValueError("invalid PGSolver vertex record")
    if buffer:
        raise ValueError("invalid PGSolver vertex record")


def game_info(path, chunk_size=1 << 16):
    """Count final declarations; the header can mean maximum ID or vertex count."""
    if chunk_size <= 0:
        raise ValueError("chunk_size must be positive")
    priorities, degrees = array("I"), array("I")
    limit = 1 << (8 * degrees.itemsize)
    sparse, count = {}, 0
    header, start = True, True
    with path.open(encoding="utf-8") as stream:
        for match in game_records(stream, chunk_size):
            if header and match["directive"] == "parity":
                header = False
                continue
            header = False
            if start and match["directive"] == "start":
                start = False
                continue
            start = False
            if match["directive"] is not None:
                raise ValueError("invalid PGSolver vertex record")
            vertex, priority = int(match[1]), int(match[2])
            degree = match[3].count(",") + 1
            if vertex not in sparse and (vertex >= len(degrees) or not degrees[vertex]):
                count += 1
            # Zero degree marks unused slots; distant IDs and oversized values stay sparse.
            if vertex <= max(1024, 2 * count) and priority < limit and degree < limit:
                if vertex >= len(degrees):
                    growth = max(vertex + 1, 2 * len(degrees), 1024) - len(degrees)
                    priorities.extend(array("I", [0]) * growth)
                    degrees.extend(array("I", [0]) * growth)
                priorities[vertex], degrees[vertex] = priority, degree
                sparse.pop(vertex, None)
            else:
                if vertex < len(degrees):
                    degrees[vertex] = 0
                sparse[vertex] = (priority, degree)
    distinct = {priority for priority, degree in zip(priorities, degrees) if degree}
    distinct.update(priority for priority, _ in sparse.values())
    edges = sum(degrees) + sum(degree for _, degree in sparse.values())
    return count, edges, len(distinct), max(distinct, default=0)


def stable_stats(stderr, root, build_dir):
    """Keep stderr in order, dropping timing lines/fields and local paths."""
    stderr = stderr.replace(str(build_dir), "build").replace(str(root), ".")
    lines = []
    timing = r"(?:\w*time\w*|elapsed\w*|seconds|duration\w*)"
    for line in stderr.splitlines():
        if re.match(r"\s*(?:wall[ -]?time|cpu[ -]?time|time|timing|elapsed|"
                    r"duration|seconds)\b", line, re.IGNORECASE):
            continue
        line = re.sub(rf"\s*\b{timing}=\S+", "", line, flags=re.IGNORECASE)
        if line.strip():
            lines.append(line.rstrip())
    return "".join(line + "\n" for line in lines)


@dataclass
class Run:
    outcome: str
    stats: str = ""
    dot: str = ""
    leaves: str = ""


def invoke(command, game, root, build_dir, timeout):
    try:
        with game.open("rb") as stream:
            result = subprocess.run(
                command, stdin=stream, capture_output=True, text=True,
                encoding="utf-8", errors="replace", cwd=root, timeout=timeout,
                env={**os.environ, "LC_ALL": "C", "LANG": "C"},
            )
    except subprocess.TimeoutExpired:
        return Run(f"timeout ({timeout:g} s)")
    except OSError:
        return Run(f"error: cannot execute {Path(command[0]).name}")
    stats = stable_stats(result.stderr, root, build_dir)
    if re.search(r"more than (?:the limit|\d+ leaves)", stats):
        return Run("host limit", stats)
    if result.returncode:
        first = stats.splitlines()[0] if stats else "process failed"
        return Run("error: " + first, stats)
    outcomes = re.findall(r"\bresult=(complete|partial)\b", stats)
    if not outcomes:
        return Run("error: missing result statistics", stats)
    return Run(outcomes[-1], stats, result.stdout)


def backend_command(backend, game, build_dir, limit):
    command = [str(build_dir / backend.binary), *backend.options,
               "--player=both", "--stats",
               "--labels=" + GAME_LABELS.get(game.stem, "sets"),
               f"--max-set-items={MAX_SET_ITEMS}"]
    if backend.binary == "pg2adot" and backend.name.startswith("lifting"):
        command.append(f"--max-host-leaves={limit}")
    return command


def run_backend(backend, game, root, build_dir, timeout, limit, *, skip_runs=False,
                compare_adaptive=True):
    if skip_runs and (game.name, backend.name) in SKIP_RUNS:
        seconds = SKIP_RUNS[game.name, backend.name]
        return Run(f"skipped (> {seconds:g} s locally)")
    command = backend_command(backend, game, build_dir, limit)
    run = invoke(command, game, root, build_dir, timeout)
    if run.outcome not in ("complete", "partial"):
        return run
    if compare_adaptive and "--adaptive-t" in backend.options:
        incremental = invoke(
            [arg.replace("--adaptive-t", "--adaptive-t=incremental")
             for arg in command], game, root, build_dir, timeout)
        if incremental.outcome not in ("complete", "partial"):
            return Run(incremental.outcome, run.stats + incremental.stats)
        assert (run.outcome == incremental.outcome and
                run.dot == incremental.dot), (
            f"{game.name}: cold/incremental adaptive trees differ ({backend.name})")
    if backend.binary == "pg2pmdot":
        leaves = invoke(command + ["--format=leaves"], game, root, build_dir,
                        timeout)
        if leaves.outcome not in ("complete", "partial"):
            return Run(leaves.outcome, leaves.stats)
        assert run.stats == leaves.stats, "DOT/leaf runs disagree"
        run.leaves = leaves.dot
    return run


def player_stats(stats):
    players = []
    for player in ("even", "odd"):
        matches = re.findall(rf"^(?:\S+ )?{player}: (.+)$", stats, re.MULTILINE)
        players.append(dict(re.findall(r"(\w+)=(\S+)", matches[-1]))
                       if matches else {})
    return players


def tree_metrics(children, root):
    """Return subtree metrics and preorder arities; None denotes an empty tree."""
    if root is None:
        return dict.fromkeys(TREE_METRICS, 0), ()
    order, leaf_depths = [], []
    widths = Counter()
    stack = [(root, 1)]
    while stack:
        name, depth = stack.pop()
        order.append(name)
        widths[depth] += 1
        if not children[name]:
            leaf_depths.append(depth)
        stack.extend((child, depth + 1) for child in reversed(children[name]))
    strahler = {}
    for name in reversed(order):
        parts = [strahler[child] for child in children[name]]
        maximum = max(parts, default=1)
        strahler[name] = maximum + (parts.count(maximum) >= 2)
    arities = tuple(len(children[name]) for name in order)
    internal = len(order) - len(leaf_depths)
    return dict(
        nodes=len(order), leaves=len(leaf_depths), internal_nodes=internal,
        height=len(widths), width=max(widths.values()), max_arity=max(arities),
        mean_internal_arity=sum(arities) / internal if internal else 0,
        branching_nodes=sum(arity >= 2 for arity in arities),
        min_leaf_depth=min(leaf_depths),
        mean_leaf_depth=sum(leaf_depths) / len(leaf_depths),
        strahler=strahler[root],
    ), arities


def format_metric(name, value):
    return f"{value:.3f}" if name.startswith("mean_") else str(value)


def tree_info(dot, pm):
    """Read our ordered DOT trees; exclude synthetic and shaved nodes."""
    if pm:
        dot = dot.rsplit("digraph ", 1)[-1]
    names = r"n\d+" if pm else r"(?:even|odd)(?:_\d+)*"
    nodes = dict(re.findall(rf"^  ({names}) \[label=(.*);$",
                            dot, re.MULTILINE))
    children = {name: [] for name in nodes}
    for parent, child in re.findall(r"^  (\w+) -> (\w+)(?: \[.*\])?;\s*$",
                                    dot, re.MULTILINE):
        if parent in nodes and child in nodes:
            children[parent].append(child)
    result = []
    for player in ("even", "odd"):
        root = next((name for name, label in nodes.items()
                     if (label.startswith('"' + player.title() + " (")
                         if pm else name == player)), None)
        if root is not None and "no non-top measures" in nodes[root]:
            root = None
        result.append(tree_metrics(children, root))
    return result


def pair(values):
    return " / ".join(map(str, values))


def run_metrics(backend, run, vertices):
    """Final trees/parameters, but wins across all shaving rounds."""
    if not run.dot:
        return dict(info=None, won=None, parameters=None)
    pm = backend.binary == "pg2pmdot"
    info = tree_info(run.dot, pm)
    stats = player_stats(run.stats)
    assert all(stats) or vertices == 0 or "residual=0" in run.stats
    won = []
    for player, current in zip(("even", "odd"), stats):
        shaved = re.search(rf"shaved_{player}=(\d+)", run.stats)
        count = int(shaved[1]) if shaved else 0
        if "--iter-shave" in backend.options:
            if pm:
                rounds = re.findall(rf"^{player}: (.+)$", run.stats, re.MULTILINE)
                count += sum(int(value) for row in rounds for value in
                             re.findall(r"\b(?:non_top|attracted)=(\d+)", row))
            else:
                count += sum(map(int, re.findall(
                    rf"^iter-shave: round=.*\b{player}=(\d+)",
                    run.stats, re.MULTILINE)))
        else:
            count += int(current.get("non_top" if pm else "region",
                                     current.get("dominion", 0)))
        won.append(count)
    if run.outcome == "complete" and vertices is not None:
        assert sum(won) == vertices, "incomplete regions"
    parameters = tuple(
        ", ".join(f"{key}={s[key]}" for key in
                  ("H", "K", "k", "t", "bits", "depth") if key in s) or "—"
        for s in stats)
    return dict(info=info, won=tuple(won), parameters=parameters)


SUMMARY_FIELDS = ["game", "backend", "outcome", "vertices", "edges",
                  "distinct_priorities", "maximum_priority"] + [
    f"{metric}_{player}" for metric in ("won", *TREE_METRICS, "parameters")
    for player in ("even", "odd")]


def metric_columns(metrics):
    if metrics["info"] is None:
        return {}
    row = {}
    for p, player in enumerate(("even", "odd")):
        row[f"won_{player}"] = metrics["won"][p]
        row[f"parameters_{player}"] = metrics["parameters"][p]
        for metric in TREE_METRICS:
            row[f"{metric}_{player}"] = format_metric(
                metric, metrics["info"][p][0][metric])
    return row


def write_run(folder, backend, run, info):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / (backend.name + ".stats")).write_text(
        f"outcome: {run.outcome}\n" + run.stats, encoding="utf-8")
    metrics = "" if info is None else "".join(
        f"{name}: even={format_metric(name, info[0][0][name])} "
        f"odd={format_metric(name, info[1][0][name])}\n"
        for name in TREE_METRICS)
    for suffix, content in (("dot", run.dot), ("leaves", run.leaves),
                            ("metrics", metrics)):
        path = folder / (backend.name + "." + suffix)
        if content:
            path.write_text(content, encoding="utf-8")
        else:
            path.unlink(missing_ok=True)


def generate(root, destination, build_dir, timeout, limit):
    lines = [
        "# Decompositions\n\n"
        "This page is generated; do not hand-edit it. See the "
        "[regeneration instructions](docs/developing.md#regenerating-example-trees).\n\n",
        "[ALGOS.md](ALGOS.md) explains how each backend builds its trees.\n\n",
        "| Column | Meaning |\n"
        "| --- | --- |\n"
        "| Backend | Solver and shaving variant; the name links to its run statistics. |\n"
        "| Outcome | `complete` assigns every vertex to a winner; `partial` proves "
        "the listed wins and leaves the rest unresolved. `host limit` means the "
        "search tree exceeded the allowed size; statistics are kept without a "
        "tree. `skipped` means an earlier local run exceeded the time shown. |\n"
        "| Won | Number of game vertices certified as wins, including all shaving "
        "rounds and their attractors, once per vertex. |\n"
        "| Nodes | Number of nodes in the result tree. Several game vertices can "
        "belong to one tree node. |\n"
        "| Leaves | Number of tree nodes with no children. |\n"
        "| Height | Number of nodes on the longest path from the root to a leaf. |\n"
        "| Parameters | " + PARAMETERS_GUIDE + " |\n"
        "| Tree | PNG links open the pictures; DOT links open their graph "
        "descriptions; leaves links list the distinct progress measures; "
        "metrics links give the final tree statistics. |\n\n",
        "Paired values, including parameters, use Even / Odd (E / O). Unknown\n"
        "winning counts are `?`; a dash means no value or tree is available.\n\n",
        "Tree metrics cover the remaining game (after preprocessing and earlier shaving rounds):\n"
        "its decomposition trees or occupied progress-measure trees, including their extra root. Empty trees have "
        "zero metrics;\n"
        "connector boxes, shaved regions and unresolved vertices do not count.\n\n",
        "The `.metrics` files have one line per field, with `even=` and `odd=` "
        "values.\n",
        TREE_METRIC_GUIDE,
        "## Preprocessing and shaving\n\n",
        "The `+pre` rows pass `--initial-shave=all --compress` before running the "
        "selected solver:\n\n",
        "1. Compress priority gaps and merge consecutive distinct priorities "
        "of the same parity, preserving winners. Choose the levels from the "
        "full input and keep them fixed throughout shaving.\n",
        "2. Shave simple winning regions in this order: all remaining "
        "vertices if their priorities have one parity; winning self-loops "
        "whose owner matches their priority's parity or whose loop is the "
        "only remaining move; cycles whose vertices are all owned by one "
        "player and whose highest priority has that player's parity. "
        "Remove each region's attractor too, Even before Odd. The attractor contains "
        "the vertices from which its winner can force play into the region. Repeat the "
        "rules until a full pass removes nothing.\n",
        "3. Run the solver on the remainder. Shaved regions appear separately "
        "and count towards Won, once per vertex. Tree metrics exclude them. "
        "If nothing remains, the result is complete with empty "
        "trees. Displayed priorities normally use the original scale; a "
        "merged level uses its largest original priority.\n\n",
        "Adaptive solvers use the remainder's priority and size bounds. "
        "Compression and shaving can lower these bounds, allowing lower t "
        "values and smaller hosts.\n\n",
        "The `+iter` rows add `--iter-shave`, available only for lifting-based "
        "backends, including progress measures. After lifting stabilises, "
        "remove certified dominions and their attractors, then retry the "
        "remainder. Fixed host bounds stay the same. Adaptive lifting advances to the "
        "next t on the smaller game, keeping the initial remaining game's host bounds.\n\n"
        "Shaved rounds show the round and t that certified them; progress "
        "measures print a graph per round with its parameters.\n\n"
        "Won counts all "
        "rounds and initial shaving, including attracted vertices, once each. "
        "Tree metrics and parameters describe only the final round: the "
        "decomposition of the remaining game or occupied measure tree, excluding earlier "
        "certificates and attracted vertices.\n\n"
        "Progress measures process Even's "
        "dominion and attractor before running Odd on the remainder. "
        "`+pre+iter` combines both steps.\n\n",
        "Source: [Oink, TACAS 2018](https://arxiv.org/abs/1801.03859), "
        "trivial dominions and priority compression.\n\n",
        "## Inventory\n",
    ]
    disagreements = []
    for game in discover_games([root / "examples"]):
        vertices, edges, priorities, maximum = game_info(game)
        print(f"{game.relative_to(root)} ({vertices} vertices)", flush=True)
        folder = destination / "decomp" / game.stem
        folder.mkdir(parents=True)
        records = []
        for backend in BACKENDS:
            name = backend.name
            run = run_backend(backend, game, root, build_dir, timeout, limit,
                              skip_runs=True)
            if run.outcome.startswith("timeout"):
                raise SystemExit(
                    f"{game.name}: safety timeout after {timeout:g} s for {name}; "
                    "measure locally and add this pair to the SKIP_RUNS skip "
                    "list in scripts/gen_decomp.py")
            metrics = run_metrics(backend, run, vertices)
            if run.outcome == "complete":
                assert all(r["won"] == metrics["won"] for r in records
                           if r["run"].outcome == "complete"), (
                               f"{game.name}: winning sizes disagree")
            write_run(folder, backend, run, metrics["info"])
            records.append(dict(name=name, backend=backend, run=run, **metrics))
            print(f"  {name}: {run.outcome}", flush=True)
        complete = next((r["won"] for r in records
                         if r["run"].outcome == "complete"), ("?", "?"))
        lines.extend([
            f"\n### {game.name}\n\n",
            "| Vertices | Edges | Distinct priorities | Maximum priority | "
            "Won (Even / Odd) |\n| ---: | ---: | ---: | ---: | --- |\n",
            f"| {vertices} | {edges} | {priorities} | {maximum} | "
            f"{pair(complete)} |\n\n",
            "| Backend | Outcome | Won E / O | Nodes E / O | Leaves E / O | "
            "Height E / O | Parameters E / O | Tree |\n",
            "| --- | --- | --- | --- | --- | --- | --- | --- |\n",
        ])
        for record in records:
            name, run, info = record["name"], record["run"], record["info"]
            link = f"decomp/{game.stem}/{name}"
            columns = [f"[{name}]({link}.stats)", run.outcome]
            if info is None:
                columns.extend(["—"] * 6)
            else:
                columns.append(pair(record["won"]))
                columns.extend(pair(part[0][name] for part in info)
                               for name in ("nodes", "leaves", "height"))
                columns.append(pair(record["parameters"]))
                columns.append(f"[PNG]({link}.png) · [DOT]({link}.dot)")
                if run.leaves:
                    columns[-1] += f" · [leaves]({link}.leaves)"
                columns[-1] += f" · [metrics]({link}.metrics)"
            lines.append("| " + " | ".join(columns) + " |\n")
        for left, right in combinations(records, 2):
            if (any((flag in left["backend"].options) !=
                    (flag in right["backend"].options)
                    for flag in ("--compress", "--iter-shave")) or
                    left["backend"].binary != right["backend"].binary or
                    left["run"].outcome != "complete" or
                    right["run"].outcome != "complete"):
                continue
            for player, (a, b) in enumerate(zip(left["info"], right["info"])):
                if a[1] != b[1]:
                    changed = [label for name, label in (
                        ("nodes", "nodes"), ("leaves", "leaves"),
                        ("height", "height"), ("strahler", "Strahler"))
                        if a[0][name] != b[0][name]]
                    player_name = "Even" if player == 0 else "Odd"
                    detail = "; " + ", ".join(changed) if changed else ""
                    disagreements.append(
                        f"| {game.name} | {player_name} | {left['name']} | "
                        f"{right['name']} | ordered shape{detail} |\n")
    lines.extend([
        "\n## Tree disagreements\n\n",
        "These pairs agree on the winners but differ in ordered tree shape. Only\n"
        "complete results with matching preprocessing and iterative-shaving choices\n"
        "are compared. Decomposition methods are compared with each other, as are\n"
        "progress-measure methods, because the two families describe different things.\n\n"
        "Trees with the same shape are omitted even if their vertex sets differ.\n"
        "Shortened picture labels cannot establish whether full sets match. See\n"
        "[the disagreement study](docs/disagreements.md)\n"
        "for comparisons of full vertex sets and smaller trees.\n\n",
        "| Game | Player | Backend A | Backend B | Differences |\n",
        "| --- | --- | --- | --- | --- |\n",
        *(disagreements or ["| — | — | — | — | No shape disagreements |\n"]),
    ])
    (destination / "DECOMP.md").write_text("".join(lines), encoding="utf-8")


def output_files(root):
    return {path.relative_to(root): path.read_bytes()
            for path in (root / "decomp").rglob("*") if path.is_file()} | (
                {Path("DECOMP.md"): (root / "DECOMP.md").read_bytes()}
                if (root / "DECOMP.md").is_file() else {})


def png_dot_hash(png):
    if not png.startswith(PNG_SIGNATURE + b"\0\0\0\rIHDR"):
        return None
    digest = None
    has_idat = False
    offset = len(PNG_SIGNATURE)
    while offset + 12 <= len(png):
        length = int.from_bytes(png[offset:offset + 4], "big")
        end = offset + 8 + length
        if end + 4 > len(png):
            return None
        chunk = png[offset + 4:end]
        if zlib.crc32(chunk) != int.from_bytes(png[end:end + 4], "big"):
            return None
        if chunk.startswith(PNG_HASH_CHUNK):
            digest = chunk[len(PNG_HASH_CHUNK):].decode("ascii", errors="replace")
        elif chunk[:4] == b"IDAT":
            has_idat = True
        elif chunk[:4] == b"IEND":
            return (digest if has_idat and length == 0 and end + 4 == len(png)
                    else None)
        offset = end + 4
    return None


def render_png(dot, digest, program):
    graphs = re.findall(rb"^digraph [^\n]*\{\n(.*?)^}\n", dot,
                        re.MULTILINE | re.DOTALL)
    if len(graphs) > 1:
        # Graphviz otherwise concatenates PNGs for multi-round progress measures.
        clusters = []
        for index, body in enumerate(graphs):
            prefix = f"round{index}_".encode("ascii")
            body = re.sub(rb"(?m)^(  )((?:n\d+|result|unresolved))(?= \[| ->)",
                          lambda m: m[1] + prefix + m[2], body)
            body = re.sub(rb"( -> )((?:n\d+|result|unresolved))\b",
                          lambda m: m[1] + prefix + m[2], body)
            clusters.append(b"subgraph cluster_" + str(index).encode("ascii")
                            + b" {\n" + body + b"}\n")
        dot = b"digraph rounds {\n" + b"".join(clusters) + b"}\n"
    try:
        result = subprocess.run([program, "-Tpng"], input=dot,
                                capture_output=True, check=True)
    except OSError as error:
        raise SystemExit(
            f"cannot render PNGs with Graphviz {program!r}: {error}; "
            "install Graphviz or use --dot PATH") from None
    except subprocess.CalledProcessError as error:
        raise SystemExit(
            f"Graphviz {program!r} failed: "
            + error.stderr.decode("utf-8", errors="replace").strip()) from None
    png = result.stdout
    ihdr_end = len(PNG_SIGNATURE) + 12 + 13
    if (not png.startswith(PNG_SIGNATURE + b"\0\0\0\rIHDR") or
            len(png) < ihdr_end):
        raise SystemExit(f"Graphviz {program!r} returned an invalid PNG")
    chunk = PNG_HASH_CHUNK + digest.encode("ascii")
    stamp = ((len(chunk) - 4).to_bytes(4, "big") + chunk
             + zlib.crc32(chunk).to_bytes(4, "big"))
    return png[:ihdr_end] + stamp + png[ihdr_end:]


def sync_outputs(root, generated, check, dot="dot"):
    old, new = output_files(root), output_files(generated)
    hashes = {path.with_suffix(".png"): sha256(data).hexdigest()
              for path, data in new.items() if path.suffix == ".dot"}
    stale = {path for path in old.keys() | new.keys()
             if path not in hashes and old.get(path) != new.get(path)}
    stale.update(path for path, digest in hashes.items()
                 if png_dot_hash(old.get(path, b"")) != digest)
    stale = sorted(stale)
    if check:
        for path in stale:
            print(f"stale: {path}")
        if stale:
            print("Regenerate: python3 scripts/gen_decomp.py")
        return bool(stale)
    for path, digest in hashes.items():
        new[path] = (render_png(new[path.with_suffix(".dot")], digest, dot)
                     if path in stale else old[path])
    for path in stale:
        target = root / path
        if path in new:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(new[path])
        else:
            target.unlink()
    size = sum(len(data) for path, data in new.items()
               if path.parts[0] == "decomp")
    print(f"decomp/: {size} bytes; {len(stale)} files updated")
    return False


def add_run_arguments(parser):
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--max-host-leaves", type=int, default=10_000)
    parser.add_argument("--dot", default=os.environ.get("STRAHLER_DOT", "dot"),
                        help="Graphviz executable (default: STRAHLER_DOT or dot)")


def validate_run_arguments(parser, args, backends):
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    if args.max_host_leaves <= 0:
        parser.error("--max-host-leaves must be positive")
    build_dir = args.build_dir.resolve()
    for binary in {backend.binary for backend in backends}:
        if not os.access(build_dir / binary, os.X_OK):
            parser.error(f"missing {binary}; run meson compile first")
    return build_dir


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    add_run_arguments(parser)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    build_dir = validate_run_arguments(parser, args, BACKENDS)
    with tempfile.TemporaryDirectory(prefix="gen-decomp-") as directory:
        generated = Path(directory)
        generate(ROOT, generated, build_dir, args.timeout,
                 args.max_host_leaves)
        return sync_outputs(ROOT, generated, args.check, args.dot)


if __name__ == "__main__":
    raise SystemExit(main())
