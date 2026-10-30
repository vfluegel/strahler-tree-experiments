# Development and command reference

## Build and validation

Build commands and requirements are in [README.md](../README.md#build).

Run shell examples from the repository root. Every executable accepts
`--version`, using the Meson project version. Meson's
`vcs_tag()` runs `git describe --tags --always --dirty` in the source directory
on every build and appends ` (git <describe>)` when available; without Git or
`.git`, only the Meson version is printed.

CI checks formatting with clang-format 18, treats compiler and clang-tidy
warnings as errors, enforces 87% line coverage, and runs ASan/UBSan and
Valgrind separately. To run the memory check locally after building:

```sh
ci/check-valgrind.sh build
```

After changing the library or a notebook, run `python scripts/execute_notebooks.py`
from the repository root with the notebook venv activated. It saves the example
outputs and static widget views for GitHub. Setup and tests are in
[jupyter-examples/README.md](../jupyter-examples/README.md).

## Streaming PGSolver reader

`pg_game_read` uses a 64 KiB `fread` buffer. Naturals accumulate with checked
`uint64_t` arithmetic; keywords use six bytes of carry, and optional ASCII
quoted names grow independently. No token depends on a buffer boundary.
The public `PGGame` and `PGParseError` API and last-declaration-wins semantics
are unchanged. An undeclared successor now reports the declaration's location
and, for example, `successor 17 (position 3) of node 5 has no final declaration`.
All existing goldens retain their contents.

One shared `uint64_t` array holds successor IDs. Sorted, unique declarations
need no sort; dense IDs use direct indexing. Otherwise only a pointer index
is sorted, obsolete edge spans are compacted, and sparse targets use binary
search. Equal-width `size_t` targets reuse the ID allocation; reordered spans
use the eventual predecessor buffer as scratch. Different-width platforms
allocate a separate target array. Declaration storage is released before
constructing predecessors whenever possible. Names retain their existing
individual ownership, avoiding a second ownership scheme in the public API.

On LP64 without names, CSR payload is about **48 bytes per vertex plus
16 bytes per edge**. Geometric successor growth retains up to 8 extra
bytes per input edge. A conservative bound for our sorted-input allocations
is `max(192*V + 16*E, 56*V + 24*E) + 65536 + 16` bytes. Here V/E count all input
declarations/edges, including replacements. Unsorted input adds an `8*V`
pointer index and can overlap declarations with the `8*E` predecessor buffer.
Allocator overhead, qsort workspace and name strings are additional. Peak
memory depends on the graph and optional names, rather than a copy of the
whole input text.

The baseline is commit `685789d`. Measured on Linux x86-64 with GCC 15.3.1,
release `pgfilt` (`-O3 -DNDEBUG`),
at nice 19 on one CPU. The generated input has 1,000,000 vertices, 5,000,000
edges and 46,333,355 text bytes, without names. Vertex i has priority i % 8,
owner i % 2 and successors i through i+4 modulo the vertex count. These are
medians of three alternating before/after runs on the same input; another
benchmark was active, so wall time is sensitive to scheduling.

| Measurement | Before | Streaming |
| --- | ---: | ---: |
| Peak RSS | 421,928 KiB (412.0 MiB) | 157,960 KiB (154.3 MiB) |
| Wall time | 1.16 s | 0.46 s |
| User / system CPU time | 0.96 / 0.19 s | 0.36 / 0.09 s |
| Overall source line coverage | 87.2% (5526/6339) | 88.7% (5643/6365) |
| Overall source branch coverage | 72.4% (4093/5654) | 73.4% (4258/5798) |

The same input fails in the old reader under 240 MiB of address space and
passes the new memory regression, including generation, in under four seconds.
Coverage uses the CI `^src/` filter, a GCC debug build with `-Db_coverage=true`,
gcovr 8.6, and the complete suite. The line gate is now 87%, with 1.7 points
of headroom. Parser line coverage rises from 88.0% to 94.3%.

The ten least-covered source files by final line coverage are:

| Source | Lines before | Lines after | Branches after |
| --- | ---: | ---: | ---: |
| `src/str-tree.cc` | 67.2% | 67.2% | 50.1% |
| `src/zielonka.c` | 66.9% | 72.0% | 72.5% |
| `src/dot_utils.c` | 77.8% | 77.8% | 65.6% |
| `src/pg_dot.c` | 80.0% | 80.0% | 72.2% |
| `src/utils.h` | 72.7% | 81.8% | 60.0% |
| `src/ordered_tree.c` | 84.4% | 84.4% | 78.3% |
| `src/stree.c` | 86.1% | 86.1% | 67.0% |
| `src/pm_tree_dot.c` | 85.6% | 86.2% | 76.3% |
| `src/ad_lift_solve.c` | 86.5% | 86.5% | 76.6% |
| `src/pg_attractor.c` | 87.5% | 87.5% | 76.2% |

Additional coverage checks actual CLI exit codes and diagnostics for malformed
games, missing files and solver limits; explicit DOT/player/priority options;
empty and non-total solver domains; and corrupted decomposition witnesses.
Unreachable helpers and debug-only traces are left untested.

## Releases

Published GitHub releases build and test all seven commands, including
`lenstree`, on Ubuntu 24.04 x86_64 and macOS 15 arm64 with Apple Clang.
The release assets are `strahler-tree-experiments-<version>-linux-x86_64.tar.gz`
and `strahler-tree-experiments-<version>-macos-arm64.tar.gz`, each containing
`bin/`, README.md, LICENSE, and LICENSE-APACHE-2.0. Dispatch
[the release workflow](../.github/workflows/release.yml) with an existing tag
to rebuild and replace its assets.

Releases also attach `strahler-<version>-py3-none-manylinux_2_28_x86_64.whl`,
`strahler-<version>-py3-none-macosx_11_0_arm64.whl`, and
`strahler-notebooks-<version>.zip`. The zip contains both wheels and everything
in `jupyter-examples/`; see its [installation instructions](../jupyter-examples/README.md).
Meson-python produces `py3-none` wheels. The shared workflow repairs them
with auditwheel on Linux and delocate on macOS, checks their tags and license
metadata, and smoke-tests each installed wheel in a fresh environment outside
the checkout. Nothing is uploaded to PyPI.

To release, push a tag `vX.Y.Z` (next: `v0.4.0`). No version file edits are needed.
CI creates the GitHub release and uploads its assets. Run
`meson test -C <build-dir> --print-errorlogs` before tagging.

Meson reads the version from `ci/version.py`: a canonical PEP 440
`STRAHLER_VERSION` override, then Git tags, then a frozen sdist `VERSION`, then
Git archive metadata. A tag `v0.3.1` gives `0.3.1`; N commits later gives
`0.3.2.devN+gHASH`. Dirty builds add `+dirty` or `.dirty` to the local part.
Fetch tags when cloning. Source archives carry their version; source copies
without metadata need `STRAHLER_VERSION`. Missing versions fail configuration.
CLI versions, Python metadata and `strahler_version()` follow Meson. The shared
library filename uses ABI version 0. CI checks the derived version against the tag.

## Regenerating example trees

[DECOMP.md](../DECOMP.md) lists the trees for every PGSolver game in
`examples/` and reports differences in their shapes. [ALGOS.md](../ALGOS.md)
explains the algorithms and metrics. Edit ALGOS.md directly.

DOT/PNG trees, progress-measure leaf streams, run statistics with timings
removed (`.stats`), and final tree statistics (`.metrics`) are in
[decomp/](../decomp/). The inventory tables show nodes, leaves and height;
the Tree column links to all metrics, including Strahler, width and arity.
Both [DECOMP.md](../DECOMP.md) and [ALGOS.md](../ALGOS.md) define them.
Build release tools, then regenerate with Python 3.10+ (standard library only).
Graphviz is needed only for new or changed pictures:

```sh
meson setup build --reconfigure --buildtype=release -Db_ndebug=true
meson compile -C build pg2adot pg2pmdot
python3 scripts/gen_decomp.py
python3 scripts/gen_decomp.py --check
```

`--build-dir DIR` selects the binaries (default `build`); `--timeout SECONDS`
sets the safety timeout (default 600 per subprocess), and `--max-host-leaves N`
sets the host limit (default 10000). Set labels show at most 8 vertex IDs per
set. Every run verifies its winning regions and trees. Each adaptive run is
also checked against incremental adaptive lifting for the same outcome and
identical DOT; one copy of the tree is stored. Progress-measure leaf streams
keep their raw first-occurrence order.

The generator tries every backend on each game, with and without preprocessing
and, where supported, iterative shaving. It skips measured slow pairs listed
in `SKIP_RUNS` in `scripts/gen_decomp.py`. These have fixed
`skipped (> N s locally)` outcomes.
A safety timeout aborts generation with a non-zero exit before any outputs
are updated. Measure that pair locally before adding it to the skip list.
Host-limit runs keep statistics and inventory rows but discard all tree
output, including any partial tree returned by an adaptive run. There are
no vertex cutoffs.

PNG files contain a `dot-sha256` text chunk with the SHA-256 hash of their
DOT source. A valid PNG with a matching hash is reused unchanged, so
regenerating only the prose needs no Graphviz installation. New, missing or
stale pictures require Graphviz; `--dot PATH` selects its executable, with
`STRAHLER_DOT` as the environment default. The generator renders all needed
pictures before updating outputs, so a rendering failure leaves the checked-in
files untouched.

`--check` reruns the tools and reports missing, changed or obsolete outputs
without writing files or rendering pictures. It exits 0 when everything is
current and non-zero otherwise. It checks PNG integrity and the stored DOT hash;
different Graphviz versions may render the same DOT differently. CI checks
freshness on PRs and non-main pushes, and refreshes generated files on pushes
to `main`.

For new benchmarks, use `python3 scripts/make_trees.py GAME_OR_DIR...`.
It uses the same backend names, limits and metrics, but ignores the examples'
skip list. Adaptive backends run once, without the inventory's cold/incremental
comparison.
`--backends LIST` selects comma-separated DECOMP.md row names;
`--out DIR` defaults to `trees/`, with one game directory per unique stem and
`summary.csv` splitting Even/Odd metrics into separate columns. Every tree has
the same `.metrics` file as the inventory. The CSV includes each `.metrics`
field with `_even` and `_odd` suffixes, alongside wins, parameters, game
metadata, outcome and the DOT path. Means have three decimal places in both
formats; metrics and parameters describe the final round, while wins include
all shaving rounds and their attractors. The build, timeout, host-limit and
Graphviz options match `gen_decomp.py`; `--no-png` disables pictures, and missing
Graphviz only warns. Progress goes to stderr;
timeouts and solver errors are recorded per pair and cause a non-zero exit.
Multi-round progress-measure PNGs show each round in a separate panel.

## Keiren benchmark measurements

[KEIREN.md](../KEIREN.md) is a static report backed by
[keiren/results.csv](../keiren/results.csv). Neither is regenerated by CI or
Meson. The runner uses Python 3.10+ and the standard library, with the same
24 backends, commands and metrics as DECOMP. Build the tools first:

```sh
meson compile -C build
python3 scripts/gen_keiren.py run --archives=mlsolver --max-games=5
python3 scripts/gen_keiren.py render
```

For the full dataset, run `python3 scripts/gen_keiren.py run`, then `render`.
Archives run in increasing size order. HTTP ranges read the ZIP directory and
one `.gm.bz2` member at a time. Each game is decompressed outside the repository,
processed, then deleted along with its solver output. The default work directory
is `$TMPDIR/strahler-keiren` (or the platform's temporary directory).
`--work-dir DIR` selects another location outside the repository. `data.yaml`
is downloaded there at the start and kept there for inspection.

Each backend gets one 30-second deadline for its solver and metric parsing.
`--timeout SECONDS` changes it. There are no adaptive comparison runs or
progress-measure leaf reruns. `--memory GIB` defaults to 2 GiB of address space
per child via `RLIMIT_AS`; macOS does not enforce this limit. `--jobs N` defaults
to three concurrent backends. Solver output goes to temporary files; a separate
capped child extracts its metrics under the same deadline. Peak RSS is the maximum
of those individual children's `os.wait4` readings. Shared input metadata is
parsed once per game in a separate child with a 600-second deadline
(`--metadata-timeout SECONDS`) and the same memory cap;
unavailable metadata does not prevent solver runs. The intended full-run host has
4 cores and 7 GB RAM. Each row records its actual machine and limits.

Results publish atomically after each game. Restart the same command to skip
recorded backend attempts, including timeouts and other solver failures.
`download failed` rows are retried and replaced on restart. Adding backends fills
missing rows. `--max-games N` limits newly attempted games across all selected
archives, so another pilot invocation moves on to the next games. Run one writer
at a time. Changing budgets does not rerun recorded attempts; remove those rows
or reset the CSV first. `--results PATH` selects another CSV.
Use `run --refresh-metadata --archives=equivchecking` to fill missing game metadata
in saved rows under the metadata deadline and the same memory cap. Each affected
game is downloaded once, and its rows publish atomically without running backends.
Solver results and details are preserved; a successful refresh clears the old
`game metadata ...` error.
CSVs without `start_winner` can still be rendered, but resuming refuses them with
a message to delete or migrate the old CSV. New CSVs use mode 0644; the ignored
`.lock` file uses normal file permissions under the current umask.

`solver_build` records the first line of `pg2adot --version` once per run, plus
Meson's build type and optimization when available. Older CSVs without this
column load with empty build cells. To label earlier measurements, use
`run --backfill-build='pg2adot 0.2 (git v0.3-4-g685789d), release -O3'`.
This fills only empty build cells and publishes atomically, with no downloads
or solver runs; it needs no build directory. Metadata refresh preserves builds.
The report counts rows and games per build and lists games with rows from
non-majority builds as reruns after parser input-buffer allocation failures.

`--archives LIST` accepts comma-separated names: `mlsolver`, `pgsolver`,
`equivchecking-hesselink`, `modelchecking`, `equivchecking`.
`--games REGEX` uses Python's `re.search` on the saved `game` path, with the
`.bz2` suffix removed, e.g. `--games='games/FLCTL.*\.gm$'`. It filters new runs,
`--retry=OUTCOMES` and `--refresh-metadata`; retries require both a matching path
and a selected outcome. Invalid regular expressions are rejected before running.
`--backends LIST`, `--build-dir DIR` and `--max-host-leaves N` select backend names,
binaries and the host limit. Keiren defaults to `SIZE_MAX`
(18,446,744,073,709,551,615 on 64-bit hosts), the largest value `pg2adot` accepts,
so time and memory budgets govern the run. DECOMP keeps its 10,000-leaf default.
Network and member extraction errors retry three times with backoff, then record
`download failed`.
An unavailable ZIP directory gets an explicit `(archive listing)` row without
pretending a game was downloaded. CSV storage failures stop the run and preserve
previously published games. A run with outstanding download failures exits 1;
solver failures stay in the CSV and do not stop the batch.

Complete backends are cross-checked on winning counts and start winners. A
disagreement is recorded and reported without aborting. Reference Booleans come
from the original game's `data.yaml` entry when a file match is unambiguous.
The [generator's cases/__init__.py](https://github.com/jkeiren/paritygame-generator/blob/master/cases/__init__.py)
maps pgsolver's true to Even winning vertex 0 and pbespgsolve's true to Even
winning the initial vertex. These games declare `start 0;`. `start_winner` records
the winner at the declared start vertex, defaulting to vertex 0 if omitted.
`reference_agrees` compares it with the reference: true means Even wins, false
means Odd wins. An unknown winner or missing reference leaves agreement blank.
Old pilot rows lack start winners and remain unchecked.

`render` reads only the CSV, needs neither games nor binaries, and writes
`KEIREN.md`. `--output PATH` changes the destination. Reports over about 300 KB
split their inventories and full winner-check lists into per-archive pages under
`keiren/`, with additional pages when an archive itself is too large. Winner
checks stay outside folded sections and count toward each page's size limit;
`KEIREN.md` keeps per-archive counts and links. All tree metrics are included in
the per-game tables; summary medians and maxima use complete runs only. Per-game
sections are folded by default; their summaries show sizes, outcome counts and
any winner disagreement or reference mismatch. Summary tables and winner checks
remain unfolded.

## Command reference

Every command accepts `--help` (stdout, exit 0) and `--version`. Use exact
long option names and attach values with `=`: `--name=value`. Short options,
abbreviations, and space-separated option values are rejected. Invalid options
or values exit 2 with one error line and `Try '<prog> --help'.` on stderr.
Options precede input operands; `--` ends option parsing. Game files and `-`
for standard input remain positional inputs in `pg2adot` and `pg2pmdot`.

Migration: `genstree` and `lenstree` use `--strahler=K`, `--log-leaves=T`,
`--height=H`, and `--leaf=L` in place of `-k K`, `-t T`, `-h H`, and `-l L`.
`genstree` also replaces `-j`, `-d`, and `-p P` with `--count`, `--dot`, and
`--p-level=P`. `pgfilt -n` becomes `pgfilt --normalize`. Former `-h` help flags
become `--help`. `str-tree INDEX [P]` becomes `str-tree --index=INDEX [--p-level=P]`.

### `genstree`

Build `U^k_{t,h}` with required `--strahler=K --log-leaves=T --height=H`
(`1 <= K <= H`, `T >= 0`).
The default output is leaf labels as `Bits` and `Blocks`. Use `--count` for the
leaf count, `--leaf=L` for one leaf (1-based), `--dot` for DOT, or `--p-level=P`
to add p-level group sizes. Counts saturate at the `unsigned` maximum, reported as
"at least" that value. Use `--help` for the full option list.

```sh
./build/genstree --strahler=4 --log-leaves=2 --height=4 --count
./build/genstree --strahler=4 --log-leaves=2 --height=4 --dot | dot -Tsvg > tree.svg
```

### `lenstree`

The Boost-based implementation computes leaf counts and individual leaf
labels. It is built when Boost is available. `--strahler=K`, `--log-leaves=T`,
and `--height=H` are required. It prints the count by default; `--leaf=L`
selects one leaf (1-based).

```sh
./build/lenstree --strahler=4 --log-leaves=2 --height=4
./build/lenstree --strahler=4 --log-leaves=2 --height=4 --leaf=3
```

### `pms2dot`

Read one line of progress-measure branches from standard input and write
their prefix tree as DOT. A branch is a comma-separated vector of bitstrings
ending in `|`; use `e` for an empty bitstring. Input order is preserved by
default.

`--check-order` rejects branches that are out of order. `--reorder` sorts them
before building the tree. These options are mutually exclusive. Bitstrings
use the paper's binary-tree order `0β < ε < 1β`, applied recursively after
equal leading bits. Vectors use the lexicographic order induced by that
bitstring order.

```sh
printf '00,e|0,e|e,e|10,e|1,e|\n' | ./build/pms2dot --check-order > tree.dot
printf '1,0|e,1|0,1|e,0|\n' | ./build/pms2dot --reorder > sorted-tree.dot
```

### `pgfilt`

Read and check one PGSolver game from standard input. By default, `pgfilt`
prints vertex/edge counts, maximum priority, owner counts and external ID
range. Use `--normalize` to print the game in a consistent PGSolver format.
An optional `start ID;` line is accepted and ignored.

Priorities are left unchanged by default. Use `--priority-mode=compact` to
remove unnecessary gaps while keeping their order and parity. Distinct
priorities remain distinct.

```sh
./build/pgfilt < game.pg
./build/pgfilt --normalize < game.pg > normalized.pg
./build/pgfilt --priority-mode=compact --normalize \
  < game.pg > compact.pg
```

### `pg2adot`

Compute and check attractor decompositions of a PGSolver game and write
them as DOT. Pass a file name, `-`, or no file name; the latter two read from
standard input. The default algorithm is attractor-decomposition lifting.

- `--player=both|even|odd` selects the trees to print (default `both`).
- `--view=classic|tree-relative|jurdzinski` selects how the decomposition is
  drawn. The default is `classic`.
- `--labels=counts|sets|none` selects node and edge labels (default `counts`).
- `--max-set-items=N` limits vertex IDs per set label (default 32).
- `--priority-mode=original|compact` selects the priority bounds shown in the
  tree. The default is `original`.
- `--algorithm=lifting|zielonka|zielonka-sym|lifting-sym` selects the solver;
  the default is `lifting`. `zielonka` runs enhanced Zielonka. All four share
  preprocessing, statistics and verification.
- `--trace=FILE` writes algorithm events as JSON Lines, leaving DOT and statistics
  unchanged. `--trace-limit=N` sets the event budget (default 1,000,000; zero is
  allowed). Exceeding it stops the solver with status 1 and a final `truncated`
  event. These options are supported with every `pg2adot` option combination;
  `pg2pmdot` does not accept them. See [the schema](#algorithm-traces).
- `--initial-shave=LIST` solves and removes dominions before building either tree.
  Use a comma list of `single` (all remaining priorities have one parity),
  `loops` (winning self-loops, including forced losing-owner loops), and
  `wcwc` (winner-controlled winning cycles), or `all` / `none` alone.
  The default is `none`. Each discovered dominion is removed with its winner's
  attractor in the current game. Rules run in the order single, loops, wcwc,
  repeating to a fixpoint as removals expose further dominions.
- `--iter-shave` attracts and removes certified dominions after lifting
  stabilises. Fixed hosts are reused until complete or a round makes no
  progress; both adaptive modes advance to the next t on the smaller game.
  It also supports `lifting-sym`; Zielonka variants reject it. DOT retains
  each shaved region's round, adaptive t, and certified decomposition, plus
  the final decomposition of the
  remaining game (after preprocessing and earlier shaving rounds). Verification
  replays the certificates and attractors; `--stats` lists rounds and vertices shaved per round.
  Restricted hosts can still end partial. `--shave` has been removed in
  favour of `--initial-shave`; there is no alias.
- `--compress` merges consecutive distinct priorities of the same parity,
  following Oink's priority compression. This is stronger than gap removal.
  `--priority-mode` still controls only display: in `original` mode a merged
  level shows its largest original priority, and an intervening empty
  bound shows that priority plus the compact offset.
- `--tree-k=K` lifts in the smaller Strahler host `U^K_{t,H}`. The result may
  be partial.
- `--adaptive-t` lifts in `U^K_{t,H}` with `t = 0, 1, ...` and the largest
  useful `K = min(H, t + 1)` until the result is complete. It reports the
  smallest such `t` on standard error, together with the smallest `t` at which
  each player's whole winning region was found. `--start-t=T` starts the
  search at `T`, and `--tree-k=K` caps `K`; a capped search can end partial.
  Both adaptive modes require `--algorithm=lifting`.
- `--adaptive-t=incremental` uses certified opponent wins as TOP seeds. It
  produces exactly the same least labellings and trees at every tried host as
  `--adaptive-t`, usually with fewer lifts. Bare `--adaptive-t` starts each
  host from bottom. Adaptive `--stats` includes `total_lifts` across all
  tried hosts.
  See [the incremental analysis](incremental-t.md) for proofs and results.
- `--kary=A` lifts in the full `A`-ary tree with each player's full height
  `H`, which is not bounded by a Strahler number. The result may be partial;
  it is complete when `A` is at least the number of vertices.
- `--tree-file=FILE` lifts in the ordered tree stored in `FILE` in the
  leaf-stream format that `pms2dot` reads, such as `0|1|`. The result may be
  partial.
- `--max-host-leaves=N` refuses to build Strahler or `A`-ary hosts with more
  than `N` leaves. The default is 1000000. With `--adaptive-t`, a later host
  limit stops the search with a warning and keeps the previous certificates
  as partial. A limit before both players have run is an error.
- `--stats` prints statistics to standard error. Ordinary lifting reports each
  player's host and its size, rounds, vertex lifts, vertices left at the top,
  and the size and Strahler number of each decomposition. Zielonka adds
  nonempty calls and attractor counts to region sizes and tree metrics.
  Symmetric variants report calls, attractors, Move/Set counts, certification
  lifts and the Even separator size; `lifting-sym` also reports host sizes.
  With preprocessing, it also reports sizes of the remaining game and shaved regions,
  and counts per rule, including attracted vertices; a vertex is counted only against the
  rule that removed it.
  Both `pg2adot` and `pg2pmdot` also print one final line such as
  `start: vertex=20 winner=even`. `vertex` is the file's `start N;` ID, defaulting
  to 0 if omitted; `winner` is `even`, `odd`, or `unresolved` if uncertified or
  absent from the game. Parsing maps the ID to its dense index after sorting and
  replacing declarations. Priority compaction and compression keep that index;
  initial and iterative shaving contribute their certified winners even after
  removal from the remaining game. Partial results report a winner only when
  certified. This line is independent of `--player` and the output format.
- `--no-verify` skips the decomposition and shaved-region closure checks and
  is intended for debugging.

Verification is on by default; shaving, compression and statistics are off.
Host choices are mutually exclusive, except `--tree-k` can cap adaptive `K`.
`--start-t` requires adaptive search. Both Zielonka variants reject host options.

`--algorithm=zielonka-sym` runs McNaughton-Zielonka with memory (Algorithm 8
of Thejaswini Raghavan's thesis). It keeps both players' decomposition labellings
across recursive calls and starts with trivial decompositions. Complete n-ary
hosts are represented lazily, so only visited nodes consume space. It always
returns both winning regions with decompositions and accepts no host options.

`--algorithm=lifting-sym` runs Algorithm 9 with asymmetric certification
(thesis §8.3). It uses the default, `--tree-k`, `--kary` or `--tree-file` hosts and
leaf limit. Both `--adaptive-t` and `--adaptive-t=incremental` are rejected:
Algorithm 9 runs on a fixed pair of hosts. With restricted hosts, Theorem F's
returned set is only a separator: final short lifts certify the two dominions,
leaving other vertices Unresolved. Universal hosts give complete results.

By default, `pg2adot` lifts in a Strahler tree that is guaranteed to contain a
decomposition of each winning region, so the result is complete. That host
grows quickly: for thousands of vertices and more than a few priorities it
exceeds the leaf limit, and `pg2adot` stops with an error. Use `--adaptive-t`,
`--tree-k`, `--kary`, or `--algorithm=zielonka` for such games.

`pg2adot` removes priority gaps internally before solving, so large numeric
gaps do not add empty levels or exhaust the recursion limit. It still shows
bounds on the original priority scale by default. Use
`--priority-mode=compact` to show the internal bounds instead.

With shaving, roots and tree metrics describe only the remaining game. A shaded
`shaved (Even)` / `shaved (Odd)` node attached to the corresponding root reports
the solved vertices separately, including their IDs with `--labels=sets`.
An empty remaining game is a complete result with no decomposition; shaved
vertices count as won when deciding completeness. Verification checks the
decomposition of the remaining game and, in the full game, that shaved regions
are closed under their
winner's attractor and that the loser cannot force an exit. The rules establish
their winning parity. The implementation follows the behavior of
[Oink](https://github.com/trolando/oink) (Apache-2.0); cycle searches use
the shared attractor code in place of Oink's SCC implementation.

```sh
./build/pg2adot game.pg > decomposition.dot
./build/pg2adot --adaptive-t --stats game.pg > decomposition.dot
./build/pg2adot --tree-k=1 --labels=sets game.pg > dominions.dot
./build/pg2adot --priority-mode=compact game.pg > compact.dot
./build/pg2adot --player=both --labels=sets --max-set-items=8 game.pg \
  | dot -Tsvg > decomposition.svg
./build/pg2adot --view=tree-relative --labels=sets game.pg \
  | dot -Tsvg > tree-relative.svg
./build/pg2adot --view=jurdzinski --labels=sets game.pg \
  | dot -Tsvg > jurdzinski.svg
```

Small sample games are in `tests/games/`:

```sh
./build/pg2adot tests/games/ordered_two_children.pg
```

### `pg2pmdot`

Lift Strahler or succinct progress measures and draw the ordered prefix tree
of each player's distinct non-top final measures. Leaves show the vertices
holding that measure. The tool reads a PGSolver file, `-`, or standard input.
`--format=dot|leaves` defaults to `dot`.

```sh
./build/pg2pmdot --algorithm=strahler --labels=sets game.pg > measures.dot
./build/pg2pmdot --algorithm=succinct --stats game.pg > measures.dot
./build/pg2pmdot --k=1 --t=1 game.pg > partial.dot
./build/pg2pmdot --algorithm=succinct --bits=0 game.pg > partial.dot
./build/pg2pmdot --format=leaves game.pg
./build/pg2pmdot --iter-shave --initial-shave=all --compress game.pg
```

`--algorithm=strahler|succinct` defaults to `strahler`. Both algorithms share
`pg2adot`'s `--player`, `--labels`, `--max-set-items`, `--priority-mode`,
`--initial-shave`, `--compress`, `--stats`, `--no-verify`, and `--version` options.
Priorities are made gap-free internally. DOT's `p` labels identify the counter
priority of each component, on the original scale by default. Preprocessing
keeps shaved vertices in separate shaded nodes; they have no progress measure.
An empty remaining game produces empty trees and empty leaf streams.

By default, each player starts from bottom on the full remaining game at every
parameter choice. Neither player's dominion is removed before the other's
run. Search stops at the first choice whose non-top sets cover the remaining
game.
Oink's counter player is the opponent of the winner named on our tree:
Even's tree uses odd counters and Odd's tree uses even counters.

Strahler search starts at `(k,t)=(1,1)` and uses Oink's priority queue with its
integer `max(k,t)/min(k,t)` comparison, pushing `(k+1,t)` before `(k,t+1)`.
`t_max=floor(log2(n))`. Full-game mode bounds `k` by
`min(t_max+2, max_depth+1)`, including the measure tree's extra root.
This bound is one level larger than Oink's outer search limit. Oink's
smaller limit can depend on attraction/removal to solve vertices that never
receive a finite measure, even at the final parameters. For example, after
`--compress`, `strahler_two.pg` needs `k=2` to give every winner a measure,
although Oink's outer limit is `k=1`. Each run caps `k` at its own
`depth+1`. Succinct search uses Oink's bounded schedule, starting at one
bit and increasing through `ceil(log2(n))`; zero/one-vertex games also support
a zero-bit budget. Depth is the number of counter-parity levels, including
empty intervening levels.

`--k=K --t=T` fixes Strahler parameters; `--bits=B` fixes succinct's budget.
These results may be partial. Roots say `partial` and, with `--player=both`,
a dashed Unresolved node lists the remaining vertices. `--stats` reports the
chosen parameters, depths, vertex counts, lifts, attempts and top counts on
standard error, followed by the same final `start: vertex=N winner=...` line
documented for `pg2adot`. Verification checks the actual truncated progress condition
on every edge (one successor for the winner, all for its opponent) and checks
dominion closure using the shared attractor code.

`--iter-shave` (formerly `--incremental`, with no alias) follows Oink's
removal order: run odd counters first, remove Even's non-top dominion and its
attractor, then run even counters on the remaining game. It prints a separate
DOT graph for every round. Roots
show that round's parameters; an `attracted` node lists removed vertices that
have no finite measure. Succinct repeats a bit budget if it made progress;
Strahler keeps Oink's original outer bound
`k_max=min(t_max+2,max_depth)` and can end partial when that search is
exhausted. Fixed parameters request just one round. Each graph describes that
round's remaining game; the trees do not accumulate wins from earlier rounds.

`--format=leaves` prints `even: ` and `odd: ` lines, with `e` for an empty
bitstring and `|` after each distinct measure. Remove the player prefix to
feed one line to `pms2dot --reorder`. Raw streams retain Oink's first occurrence
order in ascending (priority, external ID) vertex order. DOT sorts branches in the paper's order
`0β < ε < 1β`. Oink's unspecified tie ordering, its preprocessing, and its
printer of only the last remaining game can make other byte comparisons differ.
Incremental leaf output adds `# round ...` parameter lines before each pair of streams.

An optional live differential test compares both backends with pinned Oink
on seeded random games and quick examples. Set `OINK_BIN` and run
`meson test -C build oink-oracle --print-errorlogs`; see
[the oracle instructions](../tests/README.md#live-oink-oracle) for the required
upstream printer fixes and known Strahler search limit.

### `str-tree`

Check p-level successors in the tree compiled into `src/str-tree.cc`.
Use `--help` to list its options.

- With no arguments, check every leaf and its successor.
- With `--index=INDEX`, check one leaf.
- With `--index=INDEX --p-level=P`, compute that leaf's p-level successor.

`INDEX` is zero-based. `--p-level` requires `--index` and defaults to `h - 2`;
an explicit `P` must be between 0 and `h - 1`.

To compile a different generated tree into this command:

```sh
./build/genstree --strahler=3 --log-leaves=2 --height=5 --p-level=2 > examples/k3t2h5p2.hpp
python3 src/convert_out.py examples/k3t2h5p2.hpp
```

Fill in `k`, `t`, `h`, and `p` in the generated header. Update the example
include in `src/str-tree.cc` and rebuild.

Comments A through H in `src/str-tree.cc` refer to the case analysis on
page 19 of [the paper](https://arxiv.org/pdf/2003.08627).

## Reading attractor decompositions

### Complete and partial results

The default host, `--algorithm=zielonka`, and `--algorithm=zielonka-sym` always
give a complete result: the Even and Odd roots decompose the two winning
regions. A restricted host from `--tree-k`, `--kary`, `--tree-file`, or a capped
or interrupted `--adaptive-t` search still certifies that each root region is
won by its player, but it may leave some vertices unresolved. The result is
complete exactly when the two regions cover the game, even with a restricted host.

Partial roots use `D` for a certified dominion; complete roots use `W` for
the winning region. `result (synthetic; partial)` connects partial roots.
With `--player=both`, a dashed `Unresolved` node lists the remaining vertices
as `U`, without attributing them to either player. A player without a certified
dominion is shown as `D = empty; no certified dominion`.

### Classic view

Each Even or Odd root is the decomposition of that player's winning region, or
of its certified dominion `D` in a partial result.
The `result (synthetic)` node only connects the two roots when
`--player=both` is used. If a player has an empty winning region, its box says
`W=empty; no decomposition`.

Node labels describe the subtree rooted at that box:

| Label | Meaning |
| --- | --- |
| `Even` or `Odd` | The player whose decomposition this is. |
| `d` | The priority bound at this node. It has the same parity as the player. By default it uses the game's original priority scale. |
| `W` | The vertices in this node's subgame. At a root, this is the player's winning region. |
| `A` | The player's attractor within `W` to vertices with priority `d`. This can be empty if priority `d` does not occur. |
| `nodes`, `leaves` | The numbers of nodes and leaves in the complete Even or Odd tree. These totals are shown only at the root. |
| `height` | The number of nodes on the tree's longest root-to-leaf path. It is shown only at the root. |
| `Strahler` | The Strahler number of the complete Even or Odd tree. It is shown only at that tree's root. |

An edge labelled `i`, `S_i`, and `A_i` records one decomposition step:

| Label | Meaning |
| --- | --- |
| `i` | The step number. Children are shown in the order in which they were removed. |
| `S_i` | The child subgame, which is a trap for the other player. The child node's `W` is this same set. |
| `A_i` | The current player's attractor to `S_i` in the vertices remaining at step `i`. It is removed before the next step. |

With `--labels=counts`, vertical bars give set sizes: for example, `|W|=3`
means that `W` contains three vertices. With `--labels=sets`, `W={...}` also
lists their PGSolver vertex IDs. A suffix such as `+5 more` means that
`--max-set-items` hid five IDs. With `--labels=none`, nodes show only the player
and `d`, and edges have no labels.

If the input priorities contain gaps, consecutive tree nodes can have `d`
values that differ by more than two. The missing values would only create
empty levels, so they are not included. The reported node count, height, and
Strahler number describe this gap-free tree. With
`--priority-mode=compact`, child bounds decrease by two as usual.

DOT uses Graphviz's HTML-like labels for variables and subscripts.
`dot` renders them directly without LaTeX or dot2tex.

The reported Strahler number belongs to the tree that `pg2adot` built. The
command does not search for the smallest value over every possible
decomposition. Lifting and Zielonka can build different trees for the same
game; `tests/games/lifted_shape.pg` is an example.

[The disagreement study](disagreements.md) compares shapes and vertex
sets, with seeded generators, shrinking, and paired canonical goldens in
`tests/games/disagree_*.pg` and `tests/golden/disagree_*.json`.

### Tree-relative view

`--view=tree-relative` shows the same tree and priority bounds using the
notation from Section 6.1 of
[Thejaswini Raghavan's thesis](https://thejaswiniraghavan.github.io/PhD_Thesis.pdf).
The decomposition and its size stay the same; only the labels change.

At each node, the displayed sets form the disjoint partition
`V = H + T + R_1 + ... + R_k + S`:

| Label | Meaning |
| --- | --- |
| `V` | The region represented by this node. At a root it is the player's winning region. At a child it is the `R_i` set on the incoming edge. |
| `H` | Vertices in the node's core whose priority is `d`. |
| `T` | The other vertices in the player's attractor to `H`, computed inside the core. |
| `R_i` | The region passed to child `i`. It is a trap for the other player in the region remaining at that step. |
| `S` | The part of `V` outside the core. |

The core itself is `H + T + R_1 + ... + R_k`. The verifier checks this
partition, the attractors, the traps, and every recursive child. Here, "trap"
means a trap for the other player. This matches the construction in Algorithm
5 and its correspondence with classic attractor decompositions; the player
name in the prose definition in Section 6.1 points in the opposite direction.

For example, the first child in `ordered_two_children.pg` is shown as
`R_1={1,2}`. Its node has `V={1,2}`, `H={1}`, `T={}`, and `S={2}`.

### Jurdziński view

`--view=jurdzinski` expands the decomposition into the `leafy(T)` shape from
the attractor-decomposition lifting description. Nodes have simple labels
on priority levels, in place of tables.

For an Even decomposition:

- A box at even level `d` contains `H`, the vertices whose priority is exactly
  `d`.
- The `−∞` ellipse one level below contains `T`, the rest of the attractor to
  `H`.
- A numbered box is a recursive child two levels below its parent.
- The `i+` ellipse immediately after child `i` contains `S_i`, the part of the
  child's attractor outside its recursive subgame.

Thus boxes contain vertices of their exact level, while ellipses may contain
vertices of that level or lower. Odd decompositions use the same construction
with the parities reversed. Empty added leaves are omitted from the drawing.
A box with no numbered children has no `S_i` ellipses, but it may still have a
nonempty `T` ellipse one level below it.
The root summary reports the size and Strahler number of the base tree `T`,
before these leaves are added.

The row labels use the compact levels that define the tree, so adjacent rows
differ by one. With the default `--priority-mode=original`, a row also shows
its source priority bound when that number differs from its compact level.

With the default `--algorithm=lifting`, these nodes are exactly the parts of
the lifting host that the final labelling occupies; see
[attractor-decomposition lifting](#attractor-decomposition-lifting).
In a partial result the root reads `Even dominion` or `Odd dominion`.

## Tree JSON documents

All four tree-printing tools can stream one JSON document followed by one newline.
The default output remains unchanged. Object keys follow the order shown below;
there is no trailing whitespace. Vertex arrays contain sorted, unique original
PGSolver IDs, without a label cap. Children retain DOT order. Writers do not
buffer a serialized document; allocation or output errors produce a nonzero exit
status and can leave an incomplete prefix, which must not be read as a result.

Every document starts with `schema`, `version` (the tool version and Git build
identifier), `algorithm`, and `options` (the command's arguments, excluding its
executable name). Paths and options are JSON strings.
The two game tools then include `game: {vertices, edges, priorities}`: vertex
and edge counts and the sorted distinct **input** priorities, before compression.

### Decompositions: strahler-decomp/1

```sh
build/pg2adot --algorithm=zielonka --format=json game.pg > decomposition.json
```

`--format=dot|json` defaults to `dot`. JSON conflicts with any explicit `--view`,
`--labels`, or `--max-set-items`, including their default values, and exits 2.
JSON uses tree-relative sets; `--priority-mode` affects DOT display only.

Following the common header and `game`, the document contains, in order:

- `initial_won: [even, odd]`: the vertices solved by initial preprocessing.
- `complete`: boolean; false certifies only the reported dominions.
- `regions: [even, odd]`: winning regions in the remaining game, each a vertex array.
- `unresolved`: the remaining vertices, without a winning claim.
- `trees: [even, odd]`: each tree is `null` or the recursive object below.
- `shaves`: ordered entries `{certified, attracted: [even, odd]}`. Each
  `certified` is exactly `{complete, regions, unresolved, trees, shaves}` with
  these same meanings; `attracted` records the whole removed winning region.
- `metrics: {trees: [even, odd], shaves}`: metrics for the trees of the remaining game;
  `shaves[i]` recursively describes `shaves[i].certified` in the same metrics
  format. Metrics are kept outside tree objects for trace compatibility.
- `parameters: [even, odd]`: final host bounds from the same solver statistics
  as `--stats`: `H,K,t` for Strahler hosts, `H,arity` for k-ary hosts, and `H`
  for file hosts. Empty remaining games and recursive solvers have empty objects.
- `budget_exhausted`: true when an adaptive host exceeded the leaf limit;
  the returned parameters still describe the last hosts that actually ran.

A decomposition node has exactly these keys, in this order:

```json
{"h":2,"H":[0],"T":[1],"S":[],"children":[]}
```

`h` is the solver's compact priority bound (including `--compress`). At each
node, the outer region is partitioned into the core and `S` (vertices outside
that core). `H` contains the core's vertices of priority `h`; `T` contains the
stored top attractor minus `H`. Remaining core vertices belong to the ordered
children. A root's outer region and core both equal its dominion in the
remaining game, so root `S` is empty. Each child carries its own outer attractor and core trap.
A `null` tree denotes no decomposition. This object, including key order and
compact serialization, is **byte-for-byte identical** to the same tree in an
algorithm trace's `result` event. Its sets and `h` do not depend on the DOT view.

`--player=even|odd` replaces the other player's trees with `null`, recursively
including shaving certificates; regions and winning summaries remain complete.
The corresponding metrics slots are also `null`. Otherwise an absent tree has
all-zero metrics. Metrics use `ad_tree_metrics` for `nodes`, `leaves`, `height`,
and `strahler`, supplemented with DECOMP's shape metrics. Each metrics object
has these keys in order: `nodes`, `leaves`, `internal_nodes`, `height`, `width`,
`max_arity`, `mean_internal_arity`, `branching_nodes`, `min_leaf_depth`,
`mean_leaf_depth`, `strahler`. Depth counts nodes from 1; width is the largest
level size; branching nodes have at least two children. Internal nodes have
children; max arity is the largest child count. The means average child counts
over internal nodes and leaf depths over leaves, respectively, rounded to three
decimals (zero for an empty denominator). A leaf's Strahler number is 1; an
internal node takes the largest child number, adding 1 when at least two
children share that maximum. Empty trees have number 0. These metrics
describe the represented trees, including partial results, rather than claiming
a minimum over all possible decompositions. Final winners combine
`initial_won`, `regions` of the remaining game, and each shave's `attracted` arrays.

### Progress measures and prefix trees: strahler-pm/1

```sh
build/pg2pmdot --algorithm=succinct --format=json game.pg > measures.json
printf '0,e|1,e|\n' | build/pms2dot --format=json > prefix.json
```

`pg2pmdot --format=dot|leaves|json` defaults to `dot`. Its document has the common
header (`algorithm` is `strahler` or `succinct`), `game`, and `rounds`. `rounds`
is an ordered array, retaining **every** graph DOT would print under
`--iter-shave`, within one JSON document. Each round has keys:

```text
{round, complete, unresolved, trees: [even, odd]}
```

`round` is the solver's positive search-round number; gaps are possible without
iterative shaving. `complete` is true exactly when `unresolved` is empty and
has the same per-round meaning as DOT's complete/partial label. A selected
player's entry is `{parameters: {k, t, bits, depth}, tree, shaved, attracted}`.
The parameter tuple comes from the solver: `k`/`t` apply to Strahler, `bits`
to succinct; ignore the other budget fields. `depth` counts measure components.
`shaved` is the initial preprocessing summary;
`attracted` contains additional winners outside this round's measure tree.
These correspond to DOT's grey dashed children. Across rounds, winners are
the union of leaf vertices, `attracted`, and `shaved`; the last `unresolved`
array describes the remaining vertices. `--player` replaces the other
entry with `null`. A `null` inner `tree` means no non-TOP measures, corresponding
to DOT's labelled placeholder root. Node objects have exactly these keys:

```json
{"priority":3,"vertices":null,"children":[{"label":"0","node":{"priority":null,"vertices":[7],"children":[]}}]}
```

Internal nodes have a numeric `priority`, `vertices: null`, and ordered children.
Leaves have `priority: null`, the sorted vertices sharing that measure, and no
children. `priority` uses the chosen `--priority-mode` scale, matching DOT's `p`.
Every branch is `{label, node}`: `label` is its bitstring, with the empty string
representing epsilon, exactly as on the DOT edge. `--labels` and
`--max-set-items` affect DOT only; JSON always includes full information.

`pms2dot --format=dot|json` also defaults to `dot`. Its header uses
`algorithm: "prefix"`; it then has just `tree`, since its input is a measure
stream with no game or vertex mapping. Prefix nodes have only `children`, in
the same `{label, node}` format; `children: []` is a leaf. `--check-order` and
`--reorder` retain their ordinary meanings. Leaf labels are the ordered branch
labels on the root-to-leaf path, as in DOT.

### Generated Strahler trees: strahler-tree/1

```sh
build/genstree --strahler=2 --height=3 --log-leaves=1 --json > tree.json
```

`--json` is exclusive with `--dot`, `--count`, `--leaf` and `--p-level`; conflicting
output options exit 2. The common header uses `algorithm: "strahler"`, followed
by `parameters: {k, h, t}` (the supplied Strahler bound, height, and leaf-budget
exponent) and `tree`. Each node is `{children}`, with ordered branches
`{label, node}`; epsilon is the empty label and leaves have `children: []`.
The paths reproduce exactly the leaf labels and structure of `--dot`, sharing
the prefix-node shape of `pms2dot`. There is no game or vertex mapping.

`tests/json_check.py` validates these schemas without dependencies;
`tests/test_json.py` compares sets, priorities, ordered branches, shaving
summaries, metrics and trace result bytes against the existing output paths.

## Algorithm traces

For example, from the repository root:

```sh
build/pg2adot --algorithm=zielonka-sym --trace=steps.jsonl tests/games/otj22-fig1.gm >game.dot
```

Every line is one JSON object. The header has `event: "header"`,
`schema: "strahler-trace/1"`, `version`, `algorithm`, and `options` (the command
arguments). Algorithm events have consecutive `step` numbers starting at 1, `event`,
`player` (0 Even, 1 Odd), and `path` (zero-based child indices, root `[]`).
On a successful run the last and only `result` event contains the final result.
The header is outside the event budget; `truncated` replaces the first event
past the budget. After option validation, the destination is truncated and the
header written before reading either input, so a failed parse replaces any old
trace. Failed runs end in `incomplete` with a reason and the CLI exit status
whenever the stream remains writable; an unwritable stream can leave a prefix.
An `incomplete` record overrides any preceding solver result (for example, if
writing DOT subsequently fails). Files are closed on every exit path. A trace
path aliasing the game, host input, stdout, or stderr is rejected before
truncating it.

All vertex sets are sorted arrays of the original PGSolver IDs, without the DOT
label cap. Priority bounds `h` use the solver's compact priorities, including
`--compress` when requested; `--priority-mode` controls only DOT display.

| Event | Additional fields and meaning |
| --- | --- |
| `call` | Lower `player`, priority bound `h`, `paths: [even_path, odd_path]`, and `subgame`. Counts match the nonempty calls in `--stats`. |
| `return` | Lower player's returned `vertices` (the separator for the symmetric algorithms). |
| `attractor` | `kind: "top"` or `"side"`, `target`, and resulting `vertices`. The ambient `subgame` is included; lazy tests also include `safe` for their reach-through-safe attractor. |
| `set` | Replace `part: "H"`, `"T"`, or `"S"` at `path` with `vertices`; `h` and `children` describe the stored node. Discard descendants with a next child index at least `children`. |
| `move` | Transfer `vertices` from `path`/`part` to `to_path`/`to_part`. Reserved for observers that combine individual writes; the current algorithms emit sets or labels. |
| `labels` | A changed `vertex` now occupies `path`/`part`, at node bound `h`. Replace its previous position for this player. |
| `check` | `subgame`, `is_attractor_decomposition`, and `reason`, reporting the symmetric algorithm's own early-exit test. |
| `lift` | One changed `vertex`, numeric host positions `from` and `to`, plus the new `path`/`part` and `h`. Its preceding `labels` event updates replay state. |
| `round_end` | All scheduled changes of this lifting round are visible. A single-vertex schedule ends its round after one lift. |
| `phase` | `phase`, `value`, and `vertices`; some phases include a certified `result`. See below. |
| `result` | `initial_won: [even, odd]` from preprocessing, and `result: {complete, regions: [even, odd], unresolved, trees: [even, odd], shaves}`. |
| `truncated` | `step` and `limit`; no result is claimed. The solver exits 1. |
| `incomplete` | Next `step`, nonempty `reason`, and integer `exit_status`; the run failed and no result is claimed. Outside the event budget, without a player/path since input parsing can fail before solving. |

A result tree is either `null` or `{h, H, T, S, children}`, recursively. A final
result root's S is empty; vertices at root S/TOP are outside its dominion.
Intermediate replay retains root S and roots containing only S, since those
are actual algorithm positions. Apply dominion pruning only for comparisons
with materialized results. Each shaving entry
has `certified` (a result of the same format) and `attracted: [even, odd]`.
The result describes trees of the remaining game separately from earlier
shaving certificates.
Initial preprocessing is reported by `preprocessed` with its active vertices.
The final winning regions combine `initial_won`, the `regions` of the remaining
game, and each shave's `attracted` sets. The already-won vertices are the preprocessing
summary in DOT, without invented decomposition nodes.

Zielonka emits sets after stored-tree writes and uses `phase: "discard"` to
remove temporary nodes at a player's path before continuing. Symmetric solving
and lifting emit **labels at every label write**, including initialization,
smallMove, Set, short lifts, and certification. Such intermediate labels need
not define a valid decomposition. They use host paths, retaining empty host
branches; final materialization prunes unused branches in order. A regular
position is H, minus-infinity is T, a child's plus position is its S, and TOP is
root S. No attractor decomposition is substituted for unfinished labels.

`labels_reset` starts a player's new host labelling; discard that player's old
labels. Other phases are `host_built` (node count in `value`), `t_raised` (t in
`value`), `dominion_certified`, `iteration_done` (result and iteration index),
and `shave_applied` (certified result and shave index). Replayers retain the
certified trees when a shave is applied, then continue with the remaining game.
`tests/trace_replay.py` implements these rules without reading result trees
into its state, and compares its state with every final result.

The C observer is `ADTrace` in `src/ad_trace.h`: a callback, context, monotonically
increasing step counter, and failure state. All event pointers, including
`PGSet const *` and path arrays, are borrowed for the callback only. Copy what
must survive it. Returning false aborts the run. A null observer takes the null
branch at each event site and performs no observer allocation or set copying.
The existing solver entry points remain available; traced variants and
`ADLiftOptions.trace` carry the observer. Nested certification/iterative solves
share the counter and emit exactly one top-level result.

The default tests serialize and replay the bounded games, property, paper, and
host corpora. Every example/backend pair instead compares stdout, `--stats`,
and exit status with a test-only build of the same CLI whose observer counts
events and discards intermediate data (`trace_count_pg2adot`). The counting
observer writes the actual final result with the production JSON writer, so
large JSON/trace tree comparisons do not serialize every intermediate event.
These test-only summaries are not replayable full traces; production `--trace`
continues to emit every event. It uses the backend's
actual options and host budget. Deliberately restricted budgets have separate
`test_trace_host-errors` and lifecycle tests.
Games and examples are split by backend to bound each test's sanitizer runtime.
The `thesis_f4.pg` lifting cases also use counting: their real default host
solves successfully, but serializing its millions of lifts exceeds
the default test budget. The other game fixtures retain JSON replay coverage
for every backend option combination.

Uncapped invariance can legitimately exceed 2³² execution hits on hot lines.
CI's gcovr 7.0 (Ubuntu 24.04) reports them as is. Newer gcovr releases treat
such counts as suspicious and stop; pass `--gcov-suspicious-hits-threshold 0`
when generating a local report with one of those.

Plain `meson test` excludes the `slow` suite. The default examples test prints
every delegated pair; the suite registers these exact pairs as individual tests:

- `cabp_swp.ehoa.pg/lifting-adaptive`
- `cabp_swp.ehoa.pg/lifting-adaptive+pre`
- `cabp_swp.ehoa.pg/lifting-adaptive+iter`
- `cabp_swp.ehoa.pg/lifting-adaptive+pre+iter`
- `cabp_swp.ehoa.pg/zielonka-sym`
- `cabp_swp.ehoa.pg/zielonka-sym+pre`
- `thesis_f4.pg/lifting`
- `thesis_f4.pg/lifting+pre`
- `thesis_f4.pg/lifting+iter`
- `thesis_f4.pg/lifting+pre+iter`

The cabp adaptive runs exceed 20 seconds even without tracing in release builds
(about 71 million lifts). The cabp Zielonka-sym pairs also have individual slow
DOT/JSON comparisons; keeping them in the default examples tests exceeds the
120-second CI budget under debug ASan/UBSan. The four full-host F₄ invariance
tests exceed 120 seconds under ASan even using the counting observer. These
invariance tests retain their real default host budgets and successful solves.

The slow DOT/JSON comparisons for `cabp_swp.ehoa.pg/pm-strahler+iter` and
`cabp_swp.ehoa.pg/pm-strahler+pre+iter` use explicit `--k=1 --t=1` budgets to
check large partial results. Their full-budget DOT references exceed the
1800-second subprocess timeout before JSON runs. Smaller game fixtures retain
the backend's default budgets to cover multiple shaving rounds.

Build the required tools and run the full slow suite explicitly with:

```sh
meson compile -C build pg2adot pg2pmdot pms2dot trace_count_pg2adot trace_check
meson test -C build --no-rebuild --setup=all --suite slow --print-errorlogs
```

`ADVerifyError` retains `message` and adds `condition`, `path_length`/`path`,
and `vertex` (a dense C index, `SIZE_MAX` when none). `AD_VERIFY_MEMORY` is an
incomplete check, never a semantic rejection. `ad_tree_check(game, subgame,
player, root, error)` restricts the stored sets and prunes unused children before
reusing the classic verifier on the induced total subgame. Error paths map back
to the original tree. This preparation is needed because the existing verifiers
compare stored sets exactly, without restricting them. An empty restriction is
valid. The check makes no dominion claim about edges leaving the subgame; result
verification additionally checks traps in the ambient domain.

`ad_lift_materialize_partial` accepts compatible labels and their non-TOP
region. It returns a candidate even for plus fibres with empty child cores,
and a caller-owned list of `AD_VERIFY_EMPTY_CORE` structural problems, located
in host paths. This is for observation; use `ad_tree_check` to test the candidate's
semantics. Solvers continue to use the strict materializer and verifiers.

## Implementation notes

### Attractor-decomposition lifting

`pg2adot` follows the lifting formulation of attractor decompositions from
[A symmetric attractor-decomposition lifting algorithm for parity
games](https://arxiv.org/abs/2010.08288), run separately for each player `P`.
The code is split into these parts:

| File | Role |
| --- | --- |
| `src/pg_attractor.c` | `pg_attractor_through`: reach a target while staying in a safe set that need not be total. |
| `src/ad_lift_tree.c` | The host tree `leafy_∞(T)`, flattened into its depth-first order. |
| `src/ad_lift.c` | The lifting loop, a direct labelling verifier, and materialization. |
| `src/ad_lift_solve.c` | Host selection, both players, and the complete or partial result. |

The base ordered tree `T` is the host or search tree. Each regular
node `η` has a level of `P`'s parity, two below its parent. The host adds a
`−∞` leaf below `η`, a `c+` leaf after each child `c`, and a top element `∞`.
In depth-first order the block of `η` is `η, −∞(η), block(c_1), c_1+, ...,
block(c_m), c_m+`, and `after(η)` is `η+`, or `∞` at the root. The `−∞` and
`c+` leaves lie one level below `η`.

A labelling maps each vertex to a host position. It must be
priority compatible: a regular position holds exactly its level, a `−∞` or
`c+` position holds priorities up to its level, and `∞` holds anything. A
vertex `v` labelled `μ(v)` is valid when:

- at a regular `η`, its edges to vertices labelled below `after(η)` are
  allowed, and `v` has one allowed edge if `P` owns it and only allowed edges
  otherwise;
- at a `−∞` or `c+` position `ℓ`, `P` can force a visit to `{μ < ℓ}` without
  leaving `{μ ≤ ℓ}`. An opponent edge out of `{μ ≤ ℓ}` is an escape, so this is
  `pg_attractor_through` and not `pg_attractor` on the safe set;
- at `∞`, always.

Starting from the least compatible labelling, each round
evaluates every vertex against the same labelling and moves every invalid
vertex to its next compatible position. If `ν` is any valid labelling with
`μ ≤ ν`, an invalid vertex `v` is valid at `ν(v)` when the others keep their
smaller `μ` labels, because lower labels only relax the regular test and only
enlarge the lazy target and safe sets. Hence `μ(v) < next(v) ≤ ν(v)`, and the
round keeps `μ ≤ ν`. The loop ends with every vertex valid, so its result is
the least valid labelling. `AD_LIFT_SCHEDULE_SINGLE` lifts one vertex at a
time and reaches the same labelling; the tests compare the two.

For any host, the vertices below `∞` form a dominion `D_P(T)` of
`P`. With `H` regular levels, the Strahler tree `U^K_{t,H}` contains every
ordered tree with fewer than `2^(t+1)` leaves, height `H`, and Strahler number
at most `K`. A decomposition of a region with `n` vertices has at most `n`
leaves, so with `t_full = ⌊log₂ n⌋` and `K = k_full = min(H, t_full + 1)` the
host contains a decomposition of `P`'s winning region, and `D_P` is that whole
region. `--adaptive-t` asks for the smallest `t` for which
`U^{min(H,t+1)}_{t,H}` already suffices. The full `n`-ary tree of height `H`
is universal for the same reason: no node of such a decomposition has more
than `n` children. Smaller hosts give possibly smaller dominions. Player `P`'s root
level is the least level of `P`'s parity that bounds all priorities, as in the
Zielonka solver.

For a regular node `η`, the classic decomposition node
has `A = fibre(η) ∪ fibre(−∞(η))`. For each child `c`, the trap `S_i` holds the
vertices labelled in `[c, after(c))`, and the attractor `A_i` adds
`fibre(c+)`. Empty host branches are pruned. In the least labelling, a vertex
in the block of a child `c` of `η` never has a `P` edge, or only opponent
edges, to labels before `c`; otherwise it would be valid at the position just
before `c`. This makes each `S_i` an opponent trap in the remaining vertices
and each `A_i` its attractor. `ad_lift_solve` checks the labelling directly
with `ad_lift_verify` and the materialized trees with both `ad_tree_verify`
and `ad_tree_relative_verify`.

### Symmetric algorithms and thesis correspondence

`src/ad_sym.c` shares the player-swapped recursion for thesis Algorithms 8
and 9. Algorithm 8 uses lazy complete hosts; Algorithm 9 uses supplied hosts
and `ad_lift_continue` to certify its final labels without resetting progress.
[The thesis correspondence and corrected entry invariant](symmetric.md)
cover errata, the added trap-closure operation, proofs and debug checks.

## C API and Python package

`src/strahler.h` is the public C ABI. Meson builds versioned `libstrahler`
with hidden symbols except `strahler_*`; the version comes from the project.
The pre-1.0 ABI is compatible within the 0.3.x series; the Python loader
validates this version before binding native functions.
The existing static libraries still link the CLIs. Each of `pg2adot`,
`pg2pmdot`, `genstree`, and `pms2dot` has a small `main` and a shared
`*_command.c` runner. Runners use explicit streams and a reentrant exact-long
option parser. Native calls copy only vertex records before preprocessing,
borrowing the immutable adjacency arrays and names. Separate calls, including
calls sharing a game, can run concurrently. Free a game only after all its
calls and callbacks return. The library changes no global getopt state,
streams, process settings, or working directory, and never calls exit/abort.

The C ABI contains:

| Function | Input and output |
| --- | --- |
| `strahler_game_parse(text, length, &game, &error)` | PGSolver bytes → opaque immutable game |
| `strahler_game_free(game)` | Release a game; NULL is allowed |
| `strahler_game_summary(game, &json, &error)` | Input IDs, priorities, owners, successors, names and start ID |
| `strahler_decompose(game, argv, argc, callback, context, &json, &error)` | pg2adot options → strahler-decomp/1 plus optional trace lines |
| `strahler_progress_measures(game, argv, argc, &json, &error)` | pg2pmdot options → strahler-pm/1 |
| `strahler_check(game, tree_json, player, subgame_json, compress, &json, &error)` | Definition 1 on a restricted subgame → located check result |
| `strahler_materialize_partial(game, labels_json, compress, &json, &error)` | Compatible labels → candidate and structural problems |
| `strahler_genstree(k, h, t, &json, &error)` | Universal ordered tree → strahler-tree/1 |
| `strahler_pms_tree(text, length, argv, argc, &json, &error)` | Leaf stream/options → strahler-pm/1 prefix tree |
| `strahler_free(pointer)` | Release any returned JSON/error string; NULL is allowed |
| `strahler_version()` | Borrowed immutable Meson version string |

Strings returned through output pointers belong to the caller. Outputs are
cleared on entry; a failing call returns no JSON. `error` may be NULL; its
message can also be NULL if allocating the message fails. `game`/`json`
output pointers are required. Input buffers and options remain caller-owned.
Text lengths are bytes, excluding a terminator; empty text and embedded NULs
are rejected. Leaf streams passed to `strahler_pms_tree` and the label
materializer are limited to 1025 components per path before recursive native
operations. `argv` contains only CLI option strings, with no program name
or filename. Output format is implicit; format/help/version/trace-file options
are rejected. Option values use `--name=value`, just as in the CLIs. Positional
input comes from the supplied game or leaf stream. Host tree files still use
the CLI's `--tree-file=PATH` option.

Statuses retain the CLI meanings: 0 success, 1 runtime/observer failure,
2 invalid argument, 3 invalid game, 4 solver/verification failure, 5 I/O
failure, with 6 added for allocation failure. No failure, resource error,
truncation or partial result is promoted to a conclusive winning claim.
A failed decomposition check is a successful API call with `ok:false`.
Callbacks run synchronously once per serialized trace line (including the
header and terminal record), borrow `(line,length)` until return, and return
nonzero to continue. `--trace-limit=N` uses the existing truncation semantics;
callback cancellation fails the call. Callback code must not unwind across C.

JSON output keeps the schemas documented above. The added game-summary schema
is necessary because result documents intentionally contain only counts:

```json
{"schema":"strahler-game/1","vertices":[{"id":42,"priority":2,"owner":0,"name":null,"successors":[42]}],"edges":1,"start":42}
```

Tree checking accepts `null` or `{h,H,T,S,children}`, with integer external
vertex IDs in each set; `subgame_json` is an ID array, or NULL for all vertices.
The decoder bounds nesting and rejects malformed JSON, duplicate keys, unknown
IDs, duplicate IDs within a set, negative/fractional/overflowing integers,
and invalid input shapes. Its string vocabulary is ASCII (including escaped
ASCII); tree keys, part names, and host leaf streams need nothing else.
Original game names are handled by the existing PGSolver reader and JSON writer.
The game is normalized exactly as in the solvers; `compress` must be 0 or 1
and must match the run that produced the tree. Restricting an empty subgame
succeeds. For other subgames the checker requires totality, restricts the
stored sets, runs `ad_tree_check`, and preserves paths before child pruning.
Check JSON has this shape:

```json
{"ok":false,"condition":4,"path":[0],"vertex":42,"message":"a domain priority exceeds the node bound","violations":[{"condition":4,"path":[0],"vertex":42,"edge":null}]}
```

`violations` adds located independent conditions without changing the original
first-failure fields or the ABI. Edges use external IDs. Set comparisons,
priority bounds, totality and traps retain one witness per condition/node; H/T/S entry
checks collect every inconsistent entry. Invalid metadata stops its branch;
dependent attractor checks require total domains and contained targets.
Python exposes immutable `Violation` records in `CheckResult.violations`.

Conditions use these stable numeric values:

| Code | Condition | Code | Condition |
| --- | --- | --- | --- |
| 0 | OK | 12 | shaving certificate |
| 1 | input | 13 | subgame totality |
| 2 | allocation | 14 | winning region |
| 3 | node | 15 | incompleteness |
| 4 | priority bound | 16 | inactive vertex |
| 5 | top attractor | 17 | host position |
| 6 | child bound | 18 | label compatibility |
| 7 | trap | 19 | dominion |
| 8 | recursive child | 20 | regular label |
| 9 | child attractor | 21 | lazy label |
| 10 | partition | 22 | empty child core |
| 11 | core | | |

The label materializer accepts the following input. `tree` is the host's
pms2dot leaf stream, `h` its normalized root bound, and `path` the child indices
in that host. `H` selects REGULAR, `T` selects MINUS_INF, `S` selects PLUS
(or TOP for the root). Omitted vertices are inactive; duplicates, invalid
paths, and incompatible priorities are rejected. It calls
`ad_lift_materialize_partial`, retaining even empty child cores and returning
`{"tree":...,"problems":[...]}` with located condition records.

```json
{"player":0,"h":2,"tree":"0|1|","labels":[{"vertex":42,"path":[0],"part":"S"}]}
```

The materializer's problems are structural diagnostics, not a validity claim.
Use `strahler_check` on the candidate and intended subgame to decide whether
it is already an attractor decomposition.

`python/strahler/` contains the dependency-free ctypes layer (`_native.py`),
public objects/functions (`__init__.py`), and shared trace replay (`_replay.py`).
`tests/trace_replay.py` imports that replay for the existing trace regressions.
Public docstrings are the API reference; [python/README.md](../python/README.md)
provides installation and runnable examples. Python checks expose the numeric
condition codes as the `Condition` IntEnum, retaining `.name` and `int()`.
Python `trace()` rejects single-player filtering to keep final replay trees
equal to the result. `-Dpython=enabled` installs pure Python files and a
shared library in the normal `libdir`. `pyproject.toml` selects
meson-python and the `python-runtime` install tag, leaving CLI installation
out of the `py3-none-<platform>` wheel. Meson-python relocates the library into
`.strahler.mesonpy.libs`; the loader checks there before the system library
search path. The wheel declares `MIT AND Apache-2.0` and includes both license
files in `dist-info/licenses`. Ordinary CLI builds keep the Python
installation disabled. `STRAHLER_LIB` accepts a development library/build dir.

`test_strahler` exercises the shared ABI; `test_strahler_alloc` recompiles its
reachable implementations with the existing allocation injector and sweeps
failing allocation positions. Both are normal Meson tests covered by the
sanitizer, coverage, and Valgrind jobs. With `-Dpython=enabled`, `test_python`
runs pytest, including exact CLI JSON comparisons, all host modes, forward
and reverse replay, located checks, paper H_k call counts, concurrent calls,
and repeated-call RSS. Measured slow corpus pairs remain explicitly selectable
in the `slow` suite. CI tests the native API and Python package on Ubuntu 24.04
x86_64 and macOS 14 arm64 using Python 3.10. The shared wheel workflow builds
with Python 3.10 for manylinux_2_28 x86_64 and macOS 11.0+ arm64, then tests the
installed wheels with Python 3.12 on Ubuntu 24.04 and macOS 15.
