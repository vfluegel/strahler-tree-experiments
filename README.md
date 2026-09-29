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
`-p P` for the sizes of the p-level groups. Leaf counts that do not fit in an
`unsigned` are reported as "at least" that maximum.

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

Compute attractor decompositions of a PGSolver game by attractor-decomposition
lifting, check them, and write them as DOT. Pass a file name, `-`, or no file
name; the latter two read from standard input.

- `--player=both|even|odd` selects the trees to print.
- `--view=classic|tree-relative|jurdzinski` selects how the decomposition is
  drawn. The default is `classic`.
- `--labels=counts|sets|none` selects the node and edge labels.
- `--max-set-items=N` limits the number of vertices shown in a set label.
- `--priority-mode=original|compact` selects the priority bounds shown in the
  tree. The default is `original`.
- `--algorithm=lifting|zielonka` selects how the decompositions are built. The
  default is `lifting`; `zielonka` runs the enhanced Zielonka algorithm for
  comparison and cannot be combined with the options below.
- `--tree-k=K` lifts in the smaller Strahler host `U^K_{t,H}`. The result may
  be partial.
- `--adaptive-k` lifts with `K = 1, 2, ...` until the result is complete and
  reports the smallest such `K` on standard error, together with the smallest
  `K` at which each player's whole winning region was found. `--start-k=K`
  starts the search at `K` instead of 1.
- `--tree-file=FILE` lifts in the ordered tree stored in `FILE` in the
  leaf-stream format that `pms2dot` reads, such as `0|1|`. The result may be
  partial.
- `--max-host-leaves=N` refuses to build Strahler hosts with more than `N`
  leaves. The default is 1000000. With `--adaptive-k`, reaching the limit
  stops the search and prints a warning; the result found so far is kept and
  labelled as partial.
- `--stats` prints lifting statistics to standard error: host sizes, `K`,
  rounds, vertex lifts, vertices left at the top, and the size and Strahler
  number of each decomposition.
- `--no-verify` skips the decomposition checks and is intended for debugging.

By default, `pg2adot` lifts in a Strahler tree that is guaranteed to contain a
decomposition of each winning region, so the result is complete. That host
grows quickly: for thousands of vertices and more than a few priorities it
exceeds the leaf limit, and `pg2adot` stops with an error. Use `--adaptive-k`,
`--tree-k`, or `--algorithm=zielonka` for such games.

`pg2adot` removes priority gaps internally before solving, so large numeric
gaps do not add empty levels or exhaust the recursion limit. It still shows
bounds on the original priority scale by default. Use
`--priority-mode=compact` to show the internal bounds instead.

<details>
<summary>How to read the trees</summary>

#### Complete and partial results

The default host and `--algorithm=zielonka` always give a complete result: the
Even and Odd roots decompose the two winning regions. A restricted host from
`--tree-k`, `--tree-file`, or an interrupted `--adaptive-k` search still
certifies that each root region is won by its player, but it may leave some
vertices unresolved. The result is complete exactly when the two regions cover
the game, even with a restricted host.

In a partial result, a root region is a certified dominion `D` rather than the
winning region `W`, and `result (synthetic; partial)` connects the roots. When
`--player=both` is used, a dashed `Unresolved` node lists the remaining
vertices as `U`. They are not attributed to either player. A player without a
certified dominion is shown as `D = empty; no certified dominion`.

#### Classic view

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

The generated DOT uses Graphviz's HTML-like labels to typeset variables and
subscripts. Ordinary `dot` renders them directly; no LaTeX or dot2tex step is
needed.

The reported Strahler number belongs to the tree that `pg2adot` built. The
command does not search for the smallest value over every possible
decomposition. Lifting and Zielonka can build different trees for the same
game; `tests/games/lifted_shape.pg` is an example.

#### Tree-relative view

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

#### Jurdziński view

`--view=jurdzinski` expands the decomposition into the `leafy(T)` shape from
the attractor-decomposition lifting description. It uses simple labels rather
than tables and places the nodes on labelled priority levels.

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
the lifting host that the final labelling occupies; see the developer notes.
In a partial result the root reads `Even dominion` or `Odd dominion`.

</details>

```sh
./build/pg2adot game.pg > decomposition.dot
./build/pg2adot --adaptive-k --stats game.pg > decomposition.dot
./build/pg2adot --tree-k=1 --labels=sets game.pg > dominions.dot
./build/pg2adot --priority-mode=compact game.pg > compact.dot
./build/pg2adot --player=both --labels=sets --max-set-items=8 game.pg \
  | dot -Tsvg > decomposition.svg
./build/pg2adot --view=tree-relative --labels=sets game.pg \
  | dot -Tsvg > tree-relative.svg
./build/pg2adot --view=jurdzinski --labels=sets game.pg \
  | dot -Tsvg > jurdzinski.svg
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

**Host.** A base ordered tree `T` is the *host* or search tree. Each regular
node `η` has a level of `P`'s parity, two below its parent. The host adds a
`−∞` leaf below `η`, a `c+` leaf after each child `c`, and a top element `∞`.
In depth-first order the block of `η` is `η, −∞(η), block(c_1), c_1+, ...,
block(c_m), c_m+`, and `after(η)` is `η+`, or `∞` at the root. The `−∞` and
`c+` leaves lie one level below `η`.

**Labellings.** A labelling maps each vertex to a host position. It must be
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

**Short lifts.** Starting from the least compatible labelling, each round
evaluates every vertex against the same labelling and moves every invalid
vertex to its next compatible position. If `ν` is any valid labelling with
`μ ≤ ν`, an invalid vertex `v` is valid at `ν(v)` when the others keep their
smaller `μ` labels, because lower labels only relax the regular test and only
enlarge the lazy target and safe sets. Hence `μ(v) < next(v) ≤ ν(v)`, and the
round keeps `μ ≤ ν`. The loop ends with every vertex valid, so its result is
the least valid labelling. `AD_LIFT_SCHEDULE_SINGLE` lifts one vertex at a
time and reaches the same labelling; the tests compare the two.

**Dominions.** For any host, the vertices below `∞` form a dominion `D_P(T)` of
`P`. With `n` vertices, `t = ⌊log₂ n⌋`, and `H` regular levels, the Strahler
tree `U^K_{t,H}` contains every ordered tree with at most `n` leaves, height
`H`, and Strahler number at most `K`. With `K = k_full = min(H, t + 1)`, it
therefore contains a decomposition of `P`'s winning region, and `D_P` is that
whole region. Smaller `K` gives a possibly smaller dominion. Player `P`'s root
level is the least level of `P`'s parity that bounds all priorities, as in the
Zielonka solver.

**Materialization.** For a regular node `η`, the classic decomposition node
has `A = fibre(η) ∪ fibre(−∞(η))`. For each child `c`, the trap `S_i` holds the
vertices labelled in `[c, after(c))`, and the attractor `A_i` adds
`fibre(c+)`. Empty host branches are pruned. In the least labelling, a vertex
in the block of a child `c` of `η` never has a `P` edge, or only opponent
edges, to labels before `c`; otherwise it would be valid at the position just
before `c`. This makes each `S_i` an opponent trap in the remaining vertices
and each `A_i` its attractor. `ad_lift_solve` checks the labelling directly
with `ad_lift_verify` and the materialized trees with both `ad_tree_verify`
and `ad_tree_relative_verify`.
