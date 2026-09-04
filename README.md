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
`--normalize` to print the game in a consistent PGSolver format. An optional
`start ID;` line is accepted and ignored.

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

Solve a PGSolver game with the enhanced Zielonka algorithm, check the resulting
attractor decompositions, and write them as DOT. Pass a file name, `-`, or no
file name; the latter two read from standard input.

- `--player=both|even|odd` selects the trees to print.
- `--view=classic|tree-relative` selects the notation used for the
  decomposition. The default is `classic`.
- `--labels=counts|sets|none` selects the node and edge labels.
- `--max-set-items=N` limits the number of vertices shown in a set label.
- `--priority-mode=original|compact` selects the priority bounds shown in the
  tree. The default is `original`.
- `--no-verify` skips the decomposition check and is intended for debugging.

`pg2adot` removes priority gaps internally before solving, so large numeric
gaps do not add empty levels or exhaust the recursion limit. It still shows
bounds on the original priority scale by default. Use
`--priority-mode=compact` to show the internal bounds instead.

#### Reading the classic view

Each Even or Odd root is the decomposition of that player's winning region.
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

The generated DOT uses Graphviz's HTML-like labels to typeset variables and
subscripts. Ordinary `dot` renders them directly; no LaTeX or dot2tex step is
needed.

The reported Strahler number belongs to the tree that `pg2adot` built. The
command does not search for the smallest value over every possible
decomposition.

#### Reading the tree-relative view

`--view=tree-relative` shows the same tree and priority bounds using the
notation from Section 6.1 of
[Thejaswini Raghavan's thesis](https://thejaswiniraghavan.github.io/PhD_Thesis.pdf).
It changes the labels, not the decomposition or its size.

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
partition, the attractors, the traps, and every recursive child. Here, “trap”
means a trap for the other player. This matches the construction in Algorithm
5 and its correspondence with classic attractor decompositions; the player
name in the prose definition in Section 6.1 points in the opposite direction.

For example, the first child in `ordered_two_children.pg` is shown as
`R_1={1,2}`. Its node has `V={1,2}`, `H={1}`, `T={}`, and `S={2}`.

```sh
./build/pg2adot game.pg > decomposition.dot
./build/pg2adot --priority-mode=compact game.pg > compact.dot
./build/pg2adot --player=both --labels=sets --max-set-items=8 game.pg \
  | dot -Tsvg > decomposition.svg
./build/pg2adot --view=tree-relative --labels=sets game.pg \
  | dot -Tsvg > tree-relative.svg
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
