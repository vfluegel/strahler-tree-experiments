# Strahler tree experiments

Command-line tools for building Strahler trees and computing attractor
decompositions and progress measures of parity games, based on
[The Strahler Number of a Parity Game](https://arxiv.org/pdf/2003.08627).

## Build

Build with Meson 1.4+, Ninja, a C23 compiler (Clang 18+, GCC 14+, or recent
Apple Clang), and a C++20 compiler. Boost enables `lenstree`; Graphviz renders
DOT and enables DOT syntax tests. On macOS: `brew install boost graphviz meson ninja`.

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

Executables go in `build/`. Release binaries for `linux-x86_64` and
`macos-arm64` are attached to [GitHub releases](https://github.com/vfluegel/strahler-tree-experiments/releases).

To release, push a tag such as `v0.4.0`; no version file edits are needed.
Builds take their version from Git tags. See [developing](docs/developing.md#releases)
for source archives and version overrides.

## Python and notebooks

Download `strahler-notebooks-<version>.zip` from
[GitHub releases](https://github.com/vfluegel/strahler-tree-experiments/releases)
for Python wheels on Linux x86_64 and macOS Apple silicon (11.0+), plus
installation instructions for pip and uv. The zip includes everything in
`jupyter-examples/` at the release tag. The [notebook index](jupyter-examples/README.md)
describes the examples in this checkout.
Wheels are also available separately on each release; they are not published
to PyPI. See [python/README.md](python/README.md) for the
Python API and installation from a checkout (Python 3.10+).

## Commands

| Command | What it does |
| --- | --- |
| `genstree` | Generate a Strahler tree, count its leaves, or print DOT/JSON. |
| `lenstree` | Count leaves or look up a leaf with the Boost-based implementation. |
| `pms2dot` | Convert progress-measure branches from standard input to a DOT/JSON prefix tree. |
| `pgfilt` | Check a PGSolver game from standard input; print statistics or normalize it. |
| `pg2adot` | Compute and verify attractor decompositions; write DOT/JSON. |
| `pg2pmdot` | Compute Strahler or succinct progress measures; write DOT/JSON or leaf streams. |
| `str-tree` | Check p-level successors in a compiled-in example tree. |

`pg2adot` and `pg2pmdot` read a PGSolver file. Use `-` or omit the filename to
read standard input:

```sh
./build/pg2adot --algorithm=lifting --adaptive-t game.pg > decomposition.dot
./build/pg2adot --algorithm=zielonka --initial-shave=all --compress game.pg > decomposition.dot
./build/pg2pmdot --algorithm=strahler --initial-shave=all --compress game.pg > measures.dot
./build/pg2adot --adaptive-t --iter-shave game.pg > decomposition.dot
./build/pg2pmdot --algorithm=succinct --labels=sets game.pg > measures.dot
./build/pg2adot --format=json game.pg > decomposition.json
./build/pg2pmdot --format=json game.pg > measures.json
./build/genstree --strahler=2 --height=3 --log-leaves=1 --json > tree.json
printf '0,e|1,e|\n' | ./build/pms2dot --format=json > prefix.json
dot -Tsvg decomposition.dot > decomposition.svg
```

`--initial-shave=all` removes simple dominions before solving. `--compress` merges
consecutive distinct priorities of the same parity. Sample games are in
[tests/games/](tests/games/).

Generate trees for a new benchmark with `scripts/make_trees.py`. It scans files
or directories recursively and writes trees, run statistics, per-tree `.metrics`
files and `summary.csv` to `trees/`; PNGs are included when Graphviz is available.
Choose backends by their DECOMP.md names with `--backends` (default: all):

```sh
python3 scripts/make_trees.py --backends zielonka,lifting-adaptive+iter --out trees benchmark/
```

[DECOMP.md](DECOMP.md) has example trees for every backend.
[KEIREN.md](KEIREN.md) records static metrics for Jeroen Keiren's parity game benchmarks.
[ALGOS.md](ALGOS.md) explains how each method builds its trees and how to
read the metrics. See [docs/](docs/) for research notes and
[docs/developing.md](docs/developing.md) for detailed command behavior,
tree notation, and development instructions.

## License

[MIT](LICENSE) covers the original CLI, public interface, tree output,
shared modules and tests, with the exception below.

### NOTICE

`src/parameter_queue.h`, `src/pm_lift.c`, `src/pm_solve.c`, `src/pm_internal.h`, `src/strpm.c`, and
`src/sspm.c` derive from [Oink](https://github.com/trolando/oink) and
[Vanessa Flügel's fork](https://github.com/vfluegel/oink), branch `print-pms`
(checkout `51c2640fe650fd18d9331cfe5133cf3605a98c31`; tree printing introduced
in `504081d`). Copyright 2017-2018 Tom van Dijk, Johannes Kepler University Linz.
Ported to C23 and modified for tree output, these files remain Apache-2.0;
see [LICENSE-APACHE-2.0](LICENSE-APACHE-2.0).
