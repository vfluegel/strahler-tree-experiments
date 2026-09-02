# Strahler Tree Experiments

Command-line tools for working with the Strahler trees described in
[The Strahler Number of a Parity Game](https://arxiv.org/pdf/2003.08627).

## Build

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

The executables are written to `build/`. Every executable accepts `--version`;
the value comes from the Meson project version, currently `0.1`.

## Commands

### `genstree`

Build a Strahler tree for `k`, `t`, and `h`. By default, `genstree` prints its
leaf labels. Use `-j` for the leaf count, `-l L` for one leaf, `-d` for DOT, or
`-p P` for the sizes of the p-level groups.

```sh
./build/genstree -k 4 -t 2 -h 4 -j
./build/genstree -k 4 -t 2 -h 4 -d | dot -Tsvg > tree.svg
```

### `lenstree`

Compute the same leaf data with the Boost-based implementation. This command
is built when Boost is available.

```sh
./build/lenstree -k 4 -t 2 -h 4
./build/lenstree -k 4 -t 2 -h 4 -l 3
```

### `pms2dot`

Read progress-measure branches from standard input and write their prefix tree
as DOT. A branch is a comma-separated vector of bitstrings ending in `|`; use
`e` for an empty bitstring.

`--check-order` rejects branches that are out of order. `--reorder` sorts them
before building the tree. Bitstrings use the paper's binary-tree order
`0β < ε < 1β`, applied recursively after equal leading bits. Vectors use the
lexicographic order induced by that bitstring order.

```sh
printf '00,e|0,e|e,e|10,e|1,e|\n' | ./build/pms2dot --check-order > tree.dot
printf '1,0|e,1|0,1|e,0|\n' | ./build/pms2dot --reorder > sorted-tree.dot
```

### `pgfilt`

Read and check one PGSolver game from standard input. By default, `pgfilt`
prints basic statistics such as the number of vertices and edges. Use
`--normalize` to print the game in a consistent PGSolver format.

```sh
./build/pgfilt < game.pg
./build/pgfilt --normalize < game.pg > normalized.pg
```

### `pg2adot`

Solve a PGSolver game with the enhanced Zielonka algorithm, check the resulting
attractor decompositions, and write them as DOT. Pass a file name, `-`, or no
file name; the latter two read from standard input.

- `--player=both|even|odd` selects the trees to print.
- `--labels=counts|sets|none` selects the node and edge labels.
- `--max-set-items=N` limits the number of vertices shown in a set label.
- `--no-verify` skips the decomposition check and is intended for debugging.

The reported Strahler number belongs to the tree that `pg2adot` built. The
command does not search for the smallest value over every possible
decomposition.

```sh
./build/pg2adot game.pg > decomposition.dot
./build/pg2adot --player=both --labels=sets --max-set-items=8 game.pg \
  | dot -Tsvg > decomposition.svg
```

The repository includes small sample games under `tests/games/`, for example:

```sh
./build/pg2adot tests/games/ordered_two_children.pg
```

### `str-tree`

Check p-level successors in the tree compiled into `src/str-tree.cc`.

- With no arguments, check every leaf and its successor.
- With `INDEX`, check one leaf.
- With `INDEX P`, compute that leaf's p-level successor.

To compile a different generated tree into this command:

```sh
./build/genstree -k 3 -t 2 -h 5 -p 2 > examples/k3t2h5p2.hpp
python3 src/convert_out.py examples/k3t2h5p2.hpp
```

Then update the included example and the matching `k`, `t`, `h`, and `p`
values in `src/str-tree.cc`, and rebuild.

## Developer notes

Comments labelled A through H in `src/str-tree.cc` refer to the case analysis on page 19
of the paper.
