# Decompositions

This page is generated; do not hand-edit it. See the [regeneration instructions](docs/developing.md#regenerating-example-trees).

[ALGOS.md](ALGOS.md) explains how each backend builds its trees.

| Column | Meaning |
| --- | --- |
| Backend | Solver and shaving variant; the name links to its run statistics. |
| Outcome | `complete` assigns every vertex to a winner; `partial` proves the listed wins and leaves the rest unresolved. `host limit` means the search tree exceeded the allowed size; statistics are kept without a tree. `skipped` means an earlier local run exceeded the time shown. |
| Won | Number of game vertices certified as wins, including all shaving rounds and their attractors, once per vertex. |
| Nodes | Number of nodes in the result tree. Several game vertices can belong to one tree node. |
| Leaves | Number of tree nodes with no children. |
| Height | Number of nodes on the longest path from the root to a leaf. |
| Parameters | Final search bounds: lifting uses `H` (host height), `K` (Strahler bound), `t` (size allowance); `pm-strahler` uses `k` (Strahler bound), `t` (size allowance), `depth` (number of measure levels); `pm-succinct` uses `bits` (bit budget per measure) and `depth`. Zielonka methods have no search parameters. |
| Tree | PNG links open the pictures; DOT links open their graph descriptions; leaves links list the distinct progress measures; metrics links give the final tree statistics. |

Paired values, including parameters, use Even / Odd (E / O). Unknown
winning counts are `?`; a dash means no value or tree is available.

Tree metrics cover the remaining game (after preprocessing and earlier shaving rounds):
its decomposition trees or occupied progress-measure trees, including their extra root. Empty trees have zero metrics;
connector boxes, shaved regions and unresolved vertices do not count.

The `.metrics` files have one line per field, with `even=` and `odd=` values.
Depth counts nodes from the root (depth 1); means have three decimal places.

- `nodes`: Number of nodes in the result tree. Several game vertices can belong to one tree node.
- `leaves`: Number of tree nodes with no children.
- `internal_nodes`: Number of tree nodes with at least one child.
- `height`: Number of nodes on the longest path from the root to a leaf.
- `width`: Largest number of nodes on one level.
- `max_arity`: Largest number of children of one node.
- `mean_internal_arity`: Mean number of children over internal nodes; zero if there are none.
- `branching_nodes`: Number of tree nodes with at least two children.
- `min_leaf_depth`: Fewest nodes on a path from the root to a leaf.
- `mean_leaf_depth`: Mean number of nodes on a path from the root to a leaf, over all leaves.
- `strahler`: A leaf has value 1. At an internal node, take the largest child value and add 1 if at least two children share that largest value. The tree's value is the root's value.

Each player's Strahler number is the smallest Strahler number among that
player's attractor decompositions; the game's Strahler number is the larger of
the two. For a complete result without shaving, the larger of the reported Even
and Odd values is an upper bound for the game's.
With shaving or partial results, the metrics describe only the represented
part of the remaining game.

## Preprocessing and shaving

The `+pre` rows pass `--initial-shave=all --compress` before running the selected solver:

1. Compress priority gaps and merge consecutive distinct priorities of the same parity, preserving winners. Choose the levels from the full input and keep them fixed throughout shaving.
2. Shave simple winning regions in this order: all remaining vertices if their priorities have one parity; winning self-loops whose owner matches their priority's parity or whose loop is the only remaining move; cycles whose vertices are all owned by one player and whose highest priority has that player's parity. Remove each region's attractor too, Even before Odd. The attractor contains the vertices from which its winner can force play into the region. Repeat the rules until a full pass removes nothing.
3. Run the solver on the remainder. Shaved regions appear separately and count towards Won, once per vertex. Tree metrics exclude them. If nothing remains, the result is complete with empty trees. Displayed priorities normally use the original scale; a merged level uses its largest original priority.

Adaptive solvers use the remainder's priority and size bounds. Compression and shaving can lower these bounds, allowing lower t values and smaller hosts.

The `+iter` rows add `--iter-shave`, available only for lifting-based backends, including progress measures. After lifting stabilises, remove certified dominions and their attractors, then retry the remainder. Fixed host bounds stay the same. Adaptive lifting advances to the next t on the smaller game, keeping the initial remaining game's host bounds.

Shaved rounds show the round and t that certified them; progress measures print a graph per round with its parameters.

Won counts all rounds and initial shaving, including attracted vertices, once each. Tree metrics and parameters describe only the final round: the decomposition of the remaining game or occupied measure tree, excluding earlier certificates and attracted vertices.

Progress measures process Even's dominion and attractor before running Odd on the remainder. `+pre+iter` combines both steps.

Source: [Oink, TACAS 2018](https://arxiv.org/abs/1801.03859), trivial dominions and priority compression.

## Inventory

### Button.tlsf.ehoa.pg

| Vertices | Edges | Distinct priorities | Maximum priority | Won (Even / Odd) |
| ---: | ---: | ---: | ---: | --- |
| 7 | 10 | 3 | 4 | 4 / 3 |

| Backend | Outcome | Won E / O | Nodes E / O | Leaves E / O | Height E / O | Parameters E / O | Tree |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [lifting](decomp/Button.tlsf.ehoa/lifting.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=2, t=2 / H=2, K=2, t=2 | [PNG](decomp/Button.tlsf.ehoa/lifting.png) · [DOT](decomp/Button.tlsf.ehoa/lifting.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting.metrics) |
| [lifting+pre](decomp/Button.tlsf.ehoa/lifting+pre.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=2, t=2 / H=2, K=2, t=2 | [PNG](decomp/Button.tlsf.ehoa/lifting+pre.png) · [DOT](decomp/Button.tlsf.ehoa/lifting+pre.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting+pre.metrics) |
| [lifting+iter](decomp/Button.tlsf.ehoa/lifting+iter.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=2, t=2 / H=2, K=2, t=2 | [PNG](decomp/Button.tlsf.ehoa/lifting+iter.png) · [DOT](decomp/Button.tlsf.ehoa/lifting+iter.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting+iter.metrics) |
| [lifting+pre+iter](decomp/Button.tlsf.ehoa/lifting+pre+iter.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=2, t=2 / H=2, K=2, t=2 | [PNG](decomp/Button.tlsf.ehoa/lifting+pre+iter.png) · [DOT](decomp/Button.tlsf.ehoa/lifting+pre+iter.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting+pre+iter.metrics) |
| [lifting-adaptive](decomp/Button.tlsf.ehoa/lifting-adaptive.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=1, t=0 / H=2, K=1, t=0 | [PNG](decomp/Button.tlsf.ehoa/lifting-adaptive.png) · [DOT](decomp/Button.tlsf.ehoa/lifting-adaptive.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting-adaptive.metrics) |
| [lifting-adaptive+pre](decomp/Button.tlsf.ehoa/lifting-adaptive+pre.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=1, t=0 / H=2, K=1, t=0 | [PNG](decomp/Button.tlsf.ehoa/lifting-adaptive+pre.png) · [DOT](decomp/Button.tlsf.ehoa/lifting-adaptive+pre.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting-adaptive+pre.metrics) |
| [lifting-adaptive+iter](decomp/Button.tlsf.ehoa/lifting-adaptive+iter.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=1, t=0 / H=2, K=1, t=0 | [PNG](decomp/Button.tlsf.ehoa/lifting-adaptive+iter.png) · [DOT](decomp/Button.tlsf.ehoa/lifting-adaptive+iter.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting-adaptive+iter.metrics) |
| [lifting-adaptive+pre+iter](decomp/Button.tlsf.ehoa/lifting-adaptive+pre+iter.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=1, t=0 / H=2, K=1, t=0 | [PNG](decomp/Button.tlsf.ehoa/lifting-adaptive+pre+iter.png) · [DOT](decomp/Button.tlsf.ehoa/lifting-adaptive+pre+iter.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting-adaptive+pre+iter.metrics) |
| [lifting-sym](decomp/Button.tlsf.ehoa/lifting-sym.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=2, t=2 / H=2, K=2, t=2 | [PNG](decomp/Button.tlsf.ehoa/lifting-sym.png) · [DOT](decomp/Button.tlsf.ehoa/lifting-sym.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting-sym.metrics) |
| [lifting-sym+pre](decomp/Button.tlsf.ehoa/lifting-sym+pre.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=2, t=2 / H=2, K=2, t=2 | [PNG](decomp/Button.tlsf.ehoa/lifting-sym+pre.png) · [DOT](decomp/Button.tlsf.ehoa/lifting-sym+pre.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting-sym+pre.metrics) |
| [lifting-sym+iter](decomp/Button.tlsf.ehoa/lifting-sym+iter.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=2, t=2 / H=2, K=2, t=2 | [PNG](decomp/Button.tlsf.ehoa/lifting-sym+iter.png) · [DOT](decomp/Button.tlsf.ehoa/lifting-sym+iter.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting-sym+iter.metrics) |
| [lifting-sym+pre+iter](decomp/Button.tlsf.ehoa/lifting-sym+pre+iter.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | H=2, K=2, t=2 / H=2, K=2, t=2 | [PNG](decomp/Button.tlsf.ehoa/lifting-sym+pre+iter.png) · [DOT](decomp/Button.tlsf.ehoa/lifting-sym+pre+iter.dot) · [metrics](decomp/Button.tlsf.ehoa/lifting-sym+pre+iter.metrics) |
| [pm-strahler](decomp/Button.tlsf.ehoa/pm-strahler.stats) | complete | 4 / 3 | 2 / 5 | 1 / 3 | 2 / 3 | k=2, t=2, depth=1 / k=2, t=2, depth=2 | [PNG](decomp/Button.tlsf.ehoa/pm-strahler.png) · [DOT](decomp/Button.tlsf.ehoa/pm-strahler.dot) · [leaves](decomp/Button.tlsf.ehoa/pm-strahler.leaves) · [metrics](decomp/Button.tlsf.ehoa/pm-strahler.metrics) |
| [pm-strahler+pre](decomp/Button.tlsf.ehoa/pm-strahler+pre.stats) | complete | 4 / 3 | 2 / 5 | 1 / 3 | 2 / 3 | k=2, t=2, depth=1 / k=2, t=2, depth=2 | [PNG](decomp/Button.tlsf.ehoa/pm-strahler+pre.png) · [DOT](decomp/Button.tlsf.ehoa/pm-strahler+pre.dot) · [leaves](decomp/Button.tlsf.ehoa/pm-strahler+pre.leaves) · [metrics](decomp/Button.tlsf.ehoa/pm-strahler+pre.metrics) |
| [pm-strahler+iter](decomp/Button.tlsf.ehoa/pm-strahler+iter.stats) | complete | 4 / 3 | 0 / 5 | 0 / 3 | 0 / 3 | k=2, t=2, depth=1 / k=2, t=2, depth=2 | [PNG](decomp/Button.tlsf.ehoa/pm-strahler+iter.png) · [DOT](decomp/Button.tlsf.ehoa/pm-strahler+iter.dot) · [leaves](decomp/Button.tlsf.ehoa/pm-strahler+iter.leaves) · [metrics](decomp/Button.tlsf.ehoa/pm-strahler+iter.metrics) |
| [pm-strahler+pre+iter](decomp/Button.tlsf.ehoa/pm-strahler+pre+iter.stats) | complete | 4 / 3 | 0 / 5 | 0 / 3 | 0 / 3 | k=2, t=2, depth=1 / k=2, t=2, depth=2 | [PNG](decomp/Button.tlsf.ehoa/pm-strahler+pre+iter.png) · [DOT](decomp/Button.tlsf.ehoa/pm-strahler+pre+iter.dot) · [leaves](decomp/Button.tlsf.ehoa/pm-strahler+pre+iter.leaves) · [metrics](decomp/Button.tlsf.ehoa/pm-strahler+pre+iter.metrics) |
| [pm-succinct](decomp/Button.tlsf.ehoa/pm-succinct.stats) | complete | 4 / 3 | 2 / 5 | 1 / 3 | 2 / 3 | bits=1, depth=1 / bits=1, depth=2 | [PNG](decomp/Button.tlsf.ehoa/pm-succinct.png) · [DOT](decomp/Button.tlsf.ehoa/pm-succinct.dot) · [leaves](decomp/Button.tlsf.ehoa/pm-succinct.leaves) · [metrics](decomp/Button.tlsf.ehoa/pm-succinct.metrics) |
| [pm-succinct+pre](decomp/Button.tlsf.ehoa/pm-succinct+pre.stats) | complete | 4 / 3 | 2 / 5 | 1 / 3 | 2 / 3 | bits=1, depth=1 / bits=1, depth=2 | [PNG](decomp/Button.tlsf.ehoa/pm-succinct+pre.png) · [DOT](decomp/Button.tlsf.ehoa/pm-succinct+pre.dot) · [leaves](decomp/Button.tlsf.ehoa/pm-succinct+pre.leaves) · [metrics](decomp/Button.tlsf.ehoa/pm-succinct+pre.metrics) |
| [pm-succinct+iter](decomp/Button.tlsf.ehoa/pm-succinct+iter.stats) | complete | 4 / 3 | 2 / 5 | 1 / 3 | 2 / 3 | bits=1, depth=1 / bits=1, depth=2 | [PNG](decomp/Button.tlsf.ehoa/pm-succinct+iter.png) · [DOT](decomp/Button.tlsf.ehoa/pm-succinct+iter.dot) · [leaves](decomp/Button.tlsf.ehoa/pm-succinct+iter.leaves) · [metrics](decomp/Button.tlsf.ehoa/pm-succinct+iter.metrics) |
| [pm-succinct+pre+iter](decomp/Button.tlsf.ehoa/pm-succinct+pre+iter.stats) | complete | 4 / 3 | 2 / 5 | 1 / 3 | 2 / 3 | bits=1, depth=1 / bits=1, depth=2 | [PNG](decomp/Button.tlsf.ehoa/pm-succinct+pre+iter.png) · [DOT](decomp/Button.tlsf.ehoa/pm-succinct+pre+iter.dot) · [leaves](decomp/Button.tlsf.ehoa/pm-succinct+pre+iter.leaves) · [metrics](decomp/Button.tlsf.ehoa/pm-succinct+pre+iter.metrics) |
| [zielonka](decomp/Button.tlsf.ehoa/zielonka.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | — / — | [PNG](decomp/Button.tlsf.ehoa/zielonka.png) · [DOT](decomp/Button.tlsf.ehoa/zielonka.dot) · [metrics](decomp/Button.tlsf.ehoa/zielonka.metrics) |
| [zielonka+pre](decomp/Button.tlsf.ehoa/zielonka+pre.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | — / — | [PNG](decomp/Button.tlsf.ehoa/zielonka+pre.png) · [DOT](decomp/Button.tlsf.ehoa/zielonka+pre.dot) · [metrics](decomp/Button.tlsf.ehoa/zielonka+pre.metrics) |
| [zielonka-sym](decomp/Button.tlsf.ehoa/zielonka-sym.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | — / — | [PNG](decomp/Button.tlsf.ehoa/zielonka-sym.png) · [DOT](decomp/Button.tlsf.ehoa/zielonka-sym.dot) · [metrics](decomp/Button.tlsf.ehoa/zielonka-sym.metrics) |
| [zielonka-sym+pre](decomp/Button.tlsf.ehoa/zielonka-sym+pre.stats) | complete | 4 / 3 | 1 / 2 | 1 / 1 | 1 / 2 | — / — | [PNG](decomp/Button.tlsf.ehoa/zielonka-sym+pre.png) · [DOT](decomp/Button.tlsf.ehoa/zielonka-sym+pre.dot) · [metrics](decomp/Button.tlsf.ehoa/zielonka-sym+pre.metrics) |

### FairSchedulern=2_compact.gm

| Vertices | Edges | Distinct priorities | Maximum priority | Won (Even / Odd) |
| ---: | ---: | ---: | ---: | --- |
| 5596 | 130825 | 13 | 4947802324994 | 5596 / 0 |

| Backend | Outcome | Won E / O | Nodes E / O | Leaves E / O | Height E / O | Parameters E / O | Tree |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [lifting](decomp/FairSchedulern=2_compact/lifting.stats) | host limit | — | — | — | — | — | — |
| [lifting+pre](decomp/FairSchedulern=2_compact/lifting+pre.stats) | host limit | — | — | — | — | — | — |
| [lifting+iter](decomp/FairSchedulern=2_compact/lifting+iter.stats) | host limit | — | — | — | — | — | — |
| [lifting+pre+iter](decomp/FairSchedulern=2_compact/lifting+pre+iter.stats) | host limit | — | — | — | — | — | — |
| [lifting-adaptive](decomp/FairSchedulern=2_compact/lifting-adaptive.stats) | complete | 5596 / 0 | 29 / 0 | 6 / 0 | 8 / 0 | H=8, K=2, t=1 / H=8, K=2, t=1 | [PNG](decomp/FairSchedulern=2_compact/lifting-adaptive.png) · [DOT](decomp/FairSchedulern=2_compact/lifting-adaptive.dot) · [metrics](decomp/FairSchedulern=2_compact/lifting-adaptive.metrics) |
| [lifting-adaptive+pre](decomp/FairSchedulern=2_compact/lifting-adaptive+pre.stats) | complete | 5596 / 0 | 17 / 0 | 6 / 0 | 5 / 0 | H=6, K=2, t=1 / H=6, K=2, t=1 | [PNG](decomp/FairSchedulern=2_compact/lifting-adaptive+pre.png) · [DOT](decomp/FairSchedulern=2_compact/lifting-adaptive+pre.dot) · [metrics](decomp/FairSchedulern=2_compact/lifting-adaptive+pre.metrics) |
| [lifting-adaptive+iter](decomp/FairSchedulern=2_compact/lifting-adaptive+iter.stats) | complete | 5596 / 0 | 22 / 0 | 5 / 0 | 7 / 0 | H=8, K=2, t=1 / H=8, K=2, t=1 | [PNG](decomp/FairSchedulern=2_compact/lifting-adaptive+iter.png) · [DOT](decomp/FairSchedulern=2_compact/lifting-adaptive+iter.dot) · [metrics](decomp/FairSchedulern=2_compact/lifting-adaptive+iter.metrics) |
| [lifting-adaptive+pre+iter](decomp/FairSchedulern=2_compact/lifting-adaptive+pre+iter.stats) | complete | 5596 / 0 | 17 / 0 | 6 / 0 | 5 / 0 | H=6, K=2, t=1 / H=6, K=2, t=1 | [PNG](decomp/FairSchedulern=2_compact/lifting-adaptive+pre+iter.png) · [DOT](decomp/FairSchedulern=2_compact/lifting-adaptive+pre+iter.dot) · [metrics](decomp/FairSchedulern=2_compact/lifting-adaptive+pre+iter.metrics) |
| [lifting-sym](decomp/FairSchedulern=2_compact/lifting-sym.stats) | host limit | — | — | — | — | — | — |
| [lifting-sym+pre](decomp/FairSchedulern=2_compact/lifting-sym+pre.stats) | host limit | — | — | — | — | — | — |
| [lifting-sym+iter](decomp/FairSchedulern=2_compact/lifting-sym+iter.stats) | host limit | — | — | — | — | — | — |
| [lifting-sym+pre+iter](decomp/FairSchedulern=2_compact/lifting-sym+pre+iter.stats) | host limit | — | — | — | — | — | — |
| [pm-strahler](decomp/FairSchedulern=2_compact/pm-strahler.stats) | complete | 5596 / 0 | 386 / 0 | 81 / 0 | 8 / 0 | k=3, t=2, depth=7 / k=3, t=2, depth=8 | [PNG](decomp/FairSchedulern=2_compact/pm-strahler.png) · [DOT](decomp/FairSchedulern=2_compact/pm-strahler.dot) · [leaves](decomp/FairSchedulern=2_compact/pm-strahler.leaves) · [metrics](decomp/FairSchedulern=2_compact/pm-strahler.metrics) |
| [pm-strahler+pre](decomp/FairSchedulern=2_compact/pm-strahler+pre.stats) | complete | 5596 / 0 | 63 / 0 | 13 / 0 | 6 / 0 | k=2, t=2, depth=5 / k=2, t=2, depth=6 | [PNG](decomp/FairSchedulern=2_compact/pm-strahler+pre.png) · [DOT](decomp/FairSchedulern=2_compact/pm-strahler+pre.dot) · [leaves](decomp/FairSchedulern=2_compact/pm-strahler+pre.leaves) · [metrics](decomp/FairSchedulern=2_compact/pm-strahler+pre.metrics) |
| [pm-strahler+iter](decomp/FairSchedulern=2_compact/pm-strahler+iter.stats) | complete | 5596 / 0 | 379 / 0 | 80 / 0 | 8 / 0 | k=3, t=2, depth=7 / k=3, t=2, depth=8 | [PNG](decomp/FairSchedulern=2_compact/pm-strahler+iter.png) · [DOT](decomp/FairSchedulern=2_compact/pm-strahler+iter.dot) · [leaves](decomp/FairSchedulern=2_compact/pm-strahler+iter.leaves) · [metrics](decomp/FairSchedulern=2_compact/pm-strahler+iter.metrics) |
| [pm-strahler+pre+iter](decomp/FairSchedulern=2_compact/pm-strahler+pre+iter.stats) | complete | 5596 / 0 | 21 / 0 | 4 / 0 | 6 / 0 | k=2, t=2, depth=5 / k=2, t=2, depth=6 | [PNG](decomp/FairSchedulern=2_compact/pm-strahler+pre+iter.png) · [DOT](decomp/FairSchedulern=2_compact/pm-strahler+pre+iter.dot) · [leaves](decomp/FairSchedulern=2_compact/pm-strahler+pre+iter.leaves) · [metrics](decomp/FairSchedulern=2_compact/pm-strahler+pre+iter.metrics) |
| [pm-succinct](decomp/FairSchedulern=2_compact/pm-succinct.stats) | complete | 5596 / 0 | 101 / 0 | 21 / 0 | 8 / 0 | bits=2, depth=7 / bits=2, depth=8 | [PNG](decomp/FairSchedulern=2_compact/pm-succinct.png) · [DOT](decomp/FairSchedulern=2_compact/pm-succinct.dot) · [leaves](decomp/FairSchedulern=2_compact/pm-succinct.leaves) · [metrics](decomp/FairSchedulern=2_compact/pm-succinct.metrics) |
| [pm-succinct+pre](decomp/FairSchedulern=2_compact/pm-succinct+pre.stats) | complete | 5596 / 0 | 92 / 0 | 23 / 0 | 6 / 0 | bits=3, depth=5 / bits=3, depth=6 | [PNG](decomp/FairSchedulern=2_compact/pm-succinct+pre.png) · [DOT](decomp/FairSchedulern=2_compact/pm-succinct+pre.dot) · [leaves](decomp/FairSchedulern=2_compact/pm-succinct+pre.leaves) · [metrics](decomp/FairSchedulern=2_compact/pm-succinct+pre.metrics) |
| [pm-succinct+iter](decomp/FairSchedulern=2_compact/pm-succinct+iter.stats) | complete | 5596 / 0 | 94 / 0 | 20 / 0 | 8 / 0 | bits=2, depth=7 / bits=2, depth=8 | [PNG](decomp/FairSchedulern=2_compact/pm-succinct+iter.png) · [DOT](decomp/FairSchedulern=2_compact/pm-succinct+iter.dot) · [leaves](decomp/FairSchedulern=2_compact/pm-succinct+iter.leaves) · [metrics](decomp/FairSchedulern=2_compact/pm-succinct+iter.metrics) |
| [pm-succinct+pre+iter](decomp/FairSchedulern=2_compact/pm-succinct+pre+iter.stats) | complete | 5596 / 0 | 87 / 0 | 22 / 0 | 6 / 0 | bits=2, depth=5 / bits=2, depth=6 | [PNG](decomp/FairSchedulern=2_compact/pm-succinct+pre+iter.png) · [DOT](decomp/FairSchedulern=2_compact/pm-succinct+pre+iter.dot) · [leaves](decomp/FairSchedulern=2_compact/pm-succinct+pre+iter.leaves) · [metrics](decomp/FairSchedulern=2_compact/pm-succinct+pre+iter.metrics) |
| [zielonka](decomp/FairSchedulern=2_compact/zielonka.stats) | complete | 5596 / 0 | 22 / 0 | 8 / 0 | 8 / 0 | — / — | [PNG](decomp/FairSchedulern=2_compact/zielonka.png) · [DOT](decomp/FairSchedulern=2_compact/zielonka.dot) · [metrics](decomp/FairSchedulern=2_compact/zielonka.metrics) |
| [zielonka+pre](decomp/FairSchedulern=2_compact/zielonka+pre.stats) | complete | 5596 / 0 | 17 / 0 | 8 / 0 | 5 / 0 | — / — | [PNG](decomp/FairSchedulern=2_compact/zielonka+pre.png) · [DOT](decomp/FairSchedulern=2_compact/zielonka+pre.dot) · [metrics](decomp/FairSchedulern=2_compact/zielonka+pre.metrics) |
| [zielonka-sym](decomp/FairSchedulern=2_compact/zielonka-sym.stats) | complete | 5596 / 0 | 22 / 0 | 8 / 0 | 8 / 0 | — / — | [PNG](decomp/FairSchedulern=2_compact/zielonka-sym.png) · [DOT](decomp/FairSchedulern=2_compact/zielonka-sym.dot) · [metrics](decomp/FairSchedulern=2_compact/zielonka-sym.metrics) |
| [zielonka-sym+pre](decomp/FairSchedulern=2_compact/zielonka-sym+pre.stats) | complete | 5596 / 0 | 17 / 0 | 8 / 0 | 5 / 0 | — / — | [PNG](decomp/FairSchedulern=2_compact/zielonka-sym+pre.png) · [DOT](decomp/FairSchedulern=2_compact/zielonka-sym+pre.dot) · [metrics](decomp/FairSchedulern=2_compact/zielonka-sym+pre.metrics) |

### cabp_swp.ehoa.pg

| Vertices | Edges | Distinct priorities | Maximum priority | Won (Even / Odd) |
| ---: | ---: | ---: | ---: | --- |
| 875281 | 1469041 | 3 | 4 | 0 / 875281 |

| Backend | Outcome | Won E / O | Nodes E / O | Leaves E / O | Height E / O | Parameters E / O | Tree |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [lifting](decomp/cabp_swp.ehoa/lifting.stats) | host limit | — | — | — | — | — | — |
| [lifting+pre](decomp/cabp_swp.ehoa/lifting+pre.stats) | host limit | — | — | — | — | — | — |
| [lifting+iter](decomp/cabp_swp.ehoa/lifting+iter.stats) | host limit | — | — | — | — | — | — |
| [lifting+pre+iter](decomp/cabp_swp.ehoa/lifting+pre+iter.stats) | host limit | — | — | — | — | — | — |
| [lifting-adaptive](decomp/cabp_swp.ehoa/lifting-adaptive.stats) | skipped (> 60 s locally) | — | — | — | — | — | — |
| [lifting-adaptive+pre](decomp/cabp_swp.ehoa/lifting-adaptive+pre.stats) | skipped (> 60 s locally) | — | — | — | — | — | — |
| [lifting-adaptive+iter](decomp/cabp_swp.ehoa/lifting-adaptive+iter.stats) | skipped (> 60 s locally) | — | — | — | — | — | — |
| [lifting-adaptive+pre+iter](decomp/cabp_swp.ehoa/lifting-adaptive+pre+iter.stats) | skipped (> 60 s locally) | — | — | — | — | — | — |
| [lifting-sym](decomp/cabp_swp.ehoa/lifting-sym.stats) | host limit | — | — | — | — | — | — |
| [lifting-sym+pre](decomp/cabp_swp.ehoa/lifting-sym+pre.stats) | host limit | — | — | — | — | — | — |
| [lifting-sym+iter](decomp/cabp_swp.ehoa/lifting-sym+iter.stats) | host limit | — | — | — | — | — | — |
| [lifting-sym+pre+iter](decomp/cabp_swp.ehoa/lifting-sym+pre+iter.stats) | host limit | — | — | — | — | — | — |
| [pm-strahler](decomp/cabp_swp.ehoa/pm-strahler.stats) | skipped (> 60 s locally) | — | — | — | — | — | — |
| [pm-strahler+pre](decomp/cabp_swp.ehoa/pm-strahler+pre.stats) | skipped (> 60 s locally) | — | — | — | — | — | — |
| [pm-strahler+iter](decomp/cabp_swp.ehoa/pm-strahler+iter.stats) | skipped (> 60 s locally) | — | — | — | — | — | — |
| [pm-strahler+pre+iter](decomp/cabp_swp.ehoa/pm-strahler+pre+iter.stats) | skipped (> 60 s locally) | — | — | — | — | — | — |
| [pm-succinct](decomp/cabp_swp.ehoa/pm-succinct.stats) | complete | 0 / 875281 | 0 / 136 | 0 / 90 | 0 / 3 | bits=5, depth=1 / bits=5, depth=2 | [PNG](decomp/cabp_swp.ehoa/pm-succinct.png) · [DOT](decomp/cabp_swp.ehoa/pm-succinct.dot) · [leaves](decomp/cabp_swp.ehoa/pm-succinct.leaves) · [metrics](decomp/cabp_swp.ehoa/pm-succinct.metrics) |
| [pm-succinct+pre](decomp/cabp_swp.ehoa/pm-succinct+pre.stats) | complete | 0 / 875281 | 0 / 136 | 0 / 90 | 0 / 3 | bits=5, depth=1 / bits=5, depth=2 | [PNG](decomp/cabp_swp.ehoa/pm-succinct+pre.png) · [DOT](decomp/cabp_swp.ehoa/pm-succinct+pre.dot) · [leaves](decomp/cabp_swp.ehoa/pm-succinct+pre.leaves) · [metrics](decomp/cabp_swp.ehoa/pm-succinct+pre.metrics) |
| [pm-succinct+iter](decomp/cabp_swp.ehoa/pm-succinct+iter.stats) | complete | 0 / 875281 | 0 / 5 | 0 / 3 | 0 / 3 | bits=1, depth=1 / bits=1, depth=2 | [PNG](decomp/cabp_swp.ehoa/pm-succinct+iter.png) · [DOT](decomp/cabp_swp.ehoa/pm-succinct+iter.dot) · [leaves](decomp/cabp_swp.ehoa/pm-succinct+iter.leaves) · [metrics](decomp/cabp_swp.ehoa/pm-succinct+iter.metrics) |
| [pm-succinct+pre+iter](decomp/cabp_swp.ehoa/pm-succinct+pre+iter.stats) | complete | 0 / 875281 | 0 / 5 | 0 / 3 | 0 / 3 | bits=1, depth=1 / bits=1, depth=2 | [PNG](decomp/cabp_swp.ehoa/pm-succinct+pre+iter.png) · [DOT](decomp/cabp_swp.ehoa/pm-succinct+pre+iter.dot) · [leaves](decomp/cabp_swp.ehoa/pm-succinct+pre+iter.leaves) · [metrics](decomp/cabp_swp.ehoa/pm-succinct+pre+iter.metrics) |
| [zielonka](decomp/cabp_swp.ehoa/zielonka.stats) | complete | 0 / 875281 | 0 / 15 | 0 / 14 | 0 / 2 | — / — | [PNG](decomp/cabp_swp.ehoa/zielonka.png) · [DOT](decomp/cabp_swp.ehoa/zielonka.dot) · [metrics](decomp/cabp_swp.ehoa/zielonka.metrics) |
| [zielonka+pre](decomp/cabp_swp.ehoa/zielonka+pre.stats) | complete | 0 / 875281 | 0 / 15 | 0 / 14 | 0 / 2 | — / — | [PNG](decomp/cabp_swp.ehoa/zielonka+pre.png) · [DOT](decomp/cabp_swp.ehoa/zielonka+pre.dot) · [metrics](decomp/cabp_swp.ehoa/zielonka+pre.metrics) |
| [zielonka-sym](decomp/cabp_swp.ehoa/zielonka-sym.stats) | complete | 0 / 875281 | 0 / 15 | 0 / 14 | 0 / 2 | — / — | [PNG](decomp/cabp_swp.ehoa/zielonka-sym.png) · [DOT](decomp/cabp_swp.ehoa/zielonka-sym.dot) · [metrics](decomp/cabp_swp.ehoa/zielonka-sym.metrics) |
| [zielonka-sym+pre](decomp/cabp_swp.ehoa/zielonka-sym+pre.stats) | complete | 0 / 875281 | 0 / 15 | 0 / 14 | 0 / 2 | — / — | [PNG](decomp/cabp_swp.ehoa/zielonka-sym+pre.png) · [DOT](decomp/cabp_swp.ehoa/zielonka-sym+pre.dot) · [metrics](decomp/cabp_swp.ehoa/zielonka-sym+pre.metrics) |

## Tree disagreements

These pairs agree on the winners but differ in ordered tree shape. Only
complete results with matching preprocessing and iterative-shaving choices
are compared. Decomposition methods are compared with each other, as are
progress-measure methods, because the two families describe different things.

Trees with the same shape are omitted even if their vertex sets differ.
Shortened picture labels cannot establish whether full sets match. See
[the disagreement study](docs/disagreements.md)
for comparisons of full vertex sets and smaller trees.

| Game | Player | Backend A | Backend B | Differences |
| --- | --- | --- | --- | --- |
| Button.tlsf.ehoa.pg | Even | pm-strahler+iter | pm-succinct+iter | ordered shape; nodes, leaves, height, Strahler |
| Button.tlsf.ehoa.pg | Even | pm-strahler+pre+iter | pm-succinct+pre+iter | ordered shape; nodes, leaves, height, Strahler |
| FairSchedulern=2_compact.gm | Even | lifting-adaptive | zielonka | ordered shape; nodes, leaves, Strahler |
| FairSchedulern=2_compact.gm | Even | lifting-adaptive | zielonka-sym | ordered shape; nodes, leaves, Strahler |
| FairSchedulern=2_compact.gm | Even | lifting-adaptive+pre | zielonka+pre | ordered shape; leaves, Strahler |
| FairSchedulern=2_compact.gm | Even | lifting-adaptive+pre | zielonka-sym+pre | ordered shape; leaves, Strahler |
| FairSchedulern=2_compact.gm | Even | pm-strahler | pm-succinct | ordered shape; nodes, leaves |
| FairSchedulern=2_compact.gm | Even | pm-strahler+pre | pm-succinct+pre | ordered shape; nodes, leaves, Strahler |
| FairSchedulern=2_compact.gm | Even | pm-strahler+iter | pm-succinct+iter | ordered shape; nodes, leaves |
| FairSchedulern=2_compact.gm | Even | pm-strahler+pre+iter | pm-succinct+pre+iter | ordered shape; nodes, leaves, Strahler |
