# Tests

Meson builds the C unit tests and runs the command-line regression cases.

- `test_stree.c` tests the reusable Strahler-tree generator.
- `test_ordered_tree.c` tests the owned compact-leaf tree, including stable
  sibling order, normalization, round trips, invalid input, DOT, and p-level
  partitions.
- `test_pg_game.c` tests whole-stream PGSolver parsing, sparse dense-index
  construction, predecessor and successor CSR, replacement declarations,
  canonical round trips, and malformed inputs.
- `test_pg_set.c` tests the owned dense-index bitset and its move semantics.
- `test_pg_attractor.c` compares the predecessor-queue attractor against an
  independent fixed-point implementation on fixed and random subgames.
- `test_zielonka.c` checks exact decomposition witnesses and metrics, verifies
  literal priority-gap wrappers, and compares fixed-seed random games with an
  exhaustive positional-strategy solver.
- `run-single-test.sh` runs one command-line golden comparison, accepting
  literal standard input or an `@file` input source.
- `run-regression.sh` regenerates or checks the complete golden suite.
- `run-with-input.sh` supplies a file on standard input for smoke tests.
- `games/` contains parser and solver fixtures.
- `golden/` contains expected command output.
- `actual/` receives ignored output from the current test run.

## Compact leaf streams

`pms2dot` and the ordered-tree library read an ordered list of root-to-leaf
paths. `0` and `1` form an edge label, `e` is an explicit empty contribution,
`,` separates components, and `|` terminates a path. The trivial one-node tree
is `|`. Empty normalized edge labels serialize as `e`. For compatibility, a
single trailing comma before `|` is accepted as a level terminator; empty
components anywhere else are rejected.

Distinct children retain the order in which their normalized labels first
occur. Duplicate paths and prefix conflicts are rejected, so leaves and
internal nodes remain distinct.

`pms2dot --check-order` requires the input branches to increase in the
bitstring-vector order from the paper, while `pms2dot --reorder` sorts them
before constructing the tree. For bitstrings this is the infinite binary
tree's depth-first (in-order) relation `0 beta < epsilon < 1 beta`, applied
recursively after common leading bits. Vectors use its lexicographic lifting.

## PGSolver games

The parser accepts an optional `parity MAX_ID;` header followed by one or more
node declarations. IDs need not be contiguous, every final declaration must
have a successor, and every successor must name a final declaration. A later
declaration replaces an earlier declaration with the same ID. Internally,
vertices are sorted by external ID and stored densely with outgoing and
predecessor CSR arrays.

`pgfilt` prints graph counts by default and `pgfilt --normalize` emits a
canonical representation suitable for round-trip tests.

`pg2adot` keeps proof metadata in an attractor-decomposition witness. Its DOT
tree contains only recursive child decompositions: top attractors are node
metadata, while traps and child attractors are edge metadata. Count and set
goldens are parsed by Graphviz during the Meson suite.

## Running the suite

Build and run from the repository root:

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

To regenerate golden files after an intentional output change:

```sh
tests/run-regression.sh --generate-goldens
```

If any diffs are reported, the script exits non-zero. To update goldens, run with `GENERATE_GOLDENS=1` (or `--generate-goldens`) and commit the resulting `tests/golden/*` files if the changes are intentional.
