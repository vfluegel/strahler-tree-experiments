# Strahler tree experiments

Tools for generating Strahler trees and computing attractor decompositions
and progress measures of parity games, based on
[The Strahler Number of a Parity Game](https://arxiv.org/pdf/2003.08627).

## Build

You need Meson 1.4+, Ninja, a C23 compiler (Clang 18+, GCC 14+, or recent
Apple Clang), and a C++20 compiler. Install Boost to build `lenstree` and
Graphviz to render DOT files. On macOS:

```sh
brew install boost graphviz meson ninja
```

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

The binaries are written to `build/`. Prebuilt binaries for Linux x86_64 and
macOS arm64 are available from [GitHub releases](https://github.com/vfluegel/strahler-tree-experiments/releases).

## Commands

| Command | What it does |
| --- | --- |
| `genstree` | Generate Strahler trees as leaf labels, DOT, or JSON; count or select leaves. |
| `lenstree` | Count leaves or select a leaf using the Boost-based implementation. |
| `pms2dot` | Read progress-measure branches from standard input and draw their prefix tree as DOT or JSON. |
| `pgfilt` | Read a PGSolver game from standard input, check it, and print statistics or normalized text. |
| `pg2adot` | Compute and verify attractor decompositions; output DOT or JSON. |
| `pg2pmdot` | Compute Strahler or succinct progress measures; output DOT, JSON, or leaf streams. |
| `str-tree` | Check p-level successors in the example tree compiled into the binary. |

Run these examples from the repository root. They use a small game from
[tests/games/](tests/games/):

```sh
./build/pgfilt < tests/games/strahler_two.pg
./build/pg2adot --adaptive-t tests/games/strahler_two.pg > decomposition.dot
dot -Tsvg decomposition.dot > decomposition.svg
./build/pg2adot --algorithm=zielonka --format=json tests/games/strahler_two.pg > decomposition.json
./build/pg2pmdot --algorithm=strahler tests/games/strahler_two.pg > measures.dot
./build/pg2pmdot --algorithm=succinct --format=json tests/games/strahler_two.pg > measures.json
```

Both solvers accept `-` or no filename to read standard input. Add
`--initial-shave=all` to remove simple dominions before solving, `--iter-shave`
to remove solved regions between lifting runs, or `--compress` to merge
consecutive distinct priorities of the same parity. Each binary's `--help`
lists its options.

To generate a tree, count its leaves, or inspect the compiled example:

```sh
./build/genstree --strahler=2 --height=3 --log-leaves=1 --dot > tree.dot
./build/genstree --strahler=2 --height=3 --log-leaves=1 --json > tree.json
./build/lenstree --strahler=2 --height=3 --log-leaves=1
printf '0,e|1,e|\n' | ./build/pms2dot > prefix.dot
./build/str-tree --index=0 --p-level=3
```

For a batch of games, `scripts/make_trees.py` writes trees, run statistics,
per-tree `.metrics` files, and `summary.csv`. It scans directories recursively
and also creates PNGs when Graphviz is installed:

```sh
python3 scripts/make_trees.py --backends zielonka,lifting-adaptive+iter --out trees tests/games/strahler_two.pg
```

## Python and notebooks

The [Jupyter notebooks](jupyter-examples/README.md) show how to build games,
compare solvers, inspect trees, and step through lifting. Start with
[01-quickstart](jupyter-examples/01-quickstart.ipynb).

Download `strahler-notebooks-<version>.zip` from
[GitHub releases](https://github.com/vfluegel/strahler-tree-experiments/releases)
for the notebooks, Python wheels, and pip/uv installation instructions. Wheels
support Python 3.10+ on Linux x86_64 and macOS Apple silicon (11.0+). They are
also available separately on each release; they are not published to PyPI.
See the [Python README](python/README.md) for the API and installation from a checkout.

## Further reading

- [Example decompositions](DECOMP.md) for every solver backend.
- [Algorithms and metrics](ALGOS.md), including how to read the output trees.
- [SIMD implementations and benchmarks](SIMD.md).
- [Host search orders](HOST-ORDER.md), including `--search=table`.
- [Keiren benchmark metrics](KEIREN.md).
- [Command reference and development guide](docs/developing.md), including tree notation and [releases](docs/developing.md#releases).
- [Research notes](docs/).

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
