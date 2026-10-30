# Keiren benchmark metrics

Source: [Parity games](https://figshare.com/articles/dataset/Parity_games/6004130). [DOI: 10.6084/m9.figshare.6004130.v1](https://doi.org/10.6084/m9.figshare.6004130.v1).

Keiren, Jeroen (2018). Parity games. figshare. Dataset. CC0. [Benchmarks for Parity Games (FSEN 2015)](https://arxiv.org/abs/1407.3121); [generator](https://github.com/jkeiren/paritygame-generator).

These are static measurements. Games and tree output are removed after each game; only the CSV and this report are kept. CI does not download or rerun this dataset.

The default budget is 30 seconds per game and backend, including solver output parsing, with a 2 GiB address-space cap per process and three concurrent backends. The intended full-run machine is Linux with 4 cores and 7 GB RAM. macOS does not enforce RLIMIT_AS. The default host-leaf limit is `SIZE_MAX` (18,446,744,073,709,551,615 on 64-bit hosts), the largest value pg2adot accepts, so time and memory budgets govern the run. DECOMP keeps its 10,000-leaf default. Adaptive comparison runs and progress-measure leaf reruns are omitted.

| Timeout (s) | Memory (GiB) | Jobs | Host leaves | Recorded machine |
| --- | --- | --- | --- | --- |
| 30 | 2 | 3 | 18446744073709551615 | Linux x86_64, 4 CPUs, 7.6 GiB RAM |

`reference_solution` preserves the original-game Boolean when an unambiguous file match exists in `data.yaml`. In the [generator's cases/__init__.py](https://github.com/jkeiren/paritygame-generator/blob/master/cases/__init__.py), pgsolver's true means Even wins vertex 0; pbespgsolve's true means Even wins the initial vertex. These games declare `start 0;`. We compare the reference with `start_winner` for the declared `start N;`, defaulting to vertex 0 when omitted: true means Even wins, false means Odd wins. `reference_agrees` is blank if the reference or a certified start winner is unavailable. Older pilot rows lack `start_winner` and remain unchecked. Winner cross-checks compare complete backends' Even/Odd counts and start winners; different winning sets of equal size with the same start winner can still go undetected.

## Dataset notes

Recorded solver builds (blank provenance is unrecorded):

| Solver build | Rows | Games |
| --- | --- | --- |
| pg2adot 0.2 (git v0.3-4-g685789d), release -O3 | 23832 | 993 |
| pg2adot 0.2 (git v0.3-6-g210e150), release -O3 | 1056 | 44 |

Games with rows from a non-majority build were rerun after earlier rows reported the parser allocation failure `failed to allocate the input buffer`:

- equivchecking: <code>equivchecking/games/ABP(BW)_Onebit_(datasize=2_capacity=1_windowsize=1)eq=branching-bisim.gm</code>
- equivchecking: <code>equivchecking/games/ABP(BW)_Onebit_(datasize=2_capacity=1_windowsize=1)eq=branching-sim.gm</code>
- equivchecking: <code>equivchecking/games/ABP(BW)_Onebit_(datasize=2_capacity=1_windowsize=1)eq=weak-bisim.gm</code>
- equivchecking: <code>equivchecking/games/ABP(BW)_Onebit_(datasize=3_capacity=1_windowsize=1)eq=branching-bisim.gm</code>
- equivchecking: <code>equivchecking/games/ABP(BW)_Onebit_(datasize=3_capacity=1_windowsize=1)eq=branching-sim.gm</code>
- equivchecking: <code>equivchecking/games/ABP(BW)_Onebit_(datasize=3_capacity=1_windowsize=1)eq=weak-bisim.gm</code>
- equivchecking: <code>equivchecking/games/ABP_Onebit_(datasize=2_capacity=1_windowsize=1)eq=branching-bisim.gm</code>
- equivchecking: <code>equivchecking/games/ABP_Onebit_(datasize=2_capacity=1_windowsize=1)eq=branching-sim.gm</code>
- equivchecking: <code>equivchecking/games/ABP_Onebit_(datasize=2_capacity=1_windowsize=1)eq=weak-bisim.gm</code>
- equivchecking: <code>equivchecking/games/ABP_Onebit_(datasize=3_capacity=1_windowsize=1)eq=branching-bisim.gm</code>
- equivchecking: <code>equivchecking/games/ABP_Onebit_(datasize=3_capacity=1_windowsize=1)eq=branching-sim.gm</code>
- equivchecking: <code>equivchecking/games/ABP_Onebit_(datasize=3_capacity=1_windowsize=1)eq=weak-bisim.gm</code>
- equivchecking: <code>equivchecking/games/CABP_Onebit_(datasize=2_capacity=1_windowsize=1)eq=strong-bisim.gm</code>
- equivchecking: <code>equivchecking/games/CABP_Onebit_(datasize=3_capacity=1_windowsize=1)eq=strong-bisim.gm</code>
- equivchecking: <code>equivchecking/games/Onebit_SWP_(datasize=3_capacity=1_windowsize=1)eq=strong-bisim.gm</code>
- equivchecking: <code>equivchecking/games/Par_Onebit_(datasize=2_capacity=1_windowsize=1)eq=branching-bisim.gm</code>
- equivchecking: <code>equivchecking/games/Par_Onebit_(datasize=2_capacity=1_windowsize=1)eq=branching-sim.gm</code>
- equivchecking: <code>equivchecking/games/Par_Onebit_(datasize=2_capacity=1_windowsize=1)eq=weak-bisim.gm</code>
- equivchecking: <code>equivchecking/games/SWP_SWP_(datasize=2_capacity=1_windowsize=2)eq=branching-bisim.gm</code>
- equivchecking: <code>equivchecking/games/SWP_SWP_(datasize=2_capacity=1_windowsize=2)eq=branching-sim.gm</code>
- equivchecking: <code>equivchecking/games/SWP_SWP_(datasize=2_capacity=1_windowsize=2)eq=weak-bisim.gm</code>
- equivchecking: <code>equivchecking/games/SWP_SWP_(datasize=3_capacity=1_windowsize=2)eq=strong-bisim.gm</code>
- equivchecking-hesselink: <code>equivchecking-hesselink/games/Hesselink_(Implementation)_Hesselink_(Specification)_(datasize=2)eq=branching-bisim.gm</code>
- equivchecking-hesselink: <code>equivchecking-hesselink/games/Hesselink_(Implementation)_Hesselink_(Specification)_(datasize=2)eq=branching-sim.gm</code>
- equivchecking-hesselink: <code>equivchecking-hesselink/games/Hesselink_(Implementation)_Hesselink_(Specification)_(datasize=2)eq=weak-bisim.gm</code>
- equivchecking-hesselink: <code>equivchecking-hesselink/games/Hesselink_(Specification)_Hesselink_(Implementation)_(datasize=2)eq=branching-bisim.gm</code>
- equivchecking-hesselink: <code>equivchecking-hesselink/games/Hesselink_(Specification)_Hesselink_(Implementation)_(datasize=2)eq=branching-sim.gm</code>
- equivchecking-hesselink: <code>equivchecking-hesselink/games/Hesselink_(Specification)_Hesselink_(Implementation)_(datasize=2)eq=weak-bisim.gm</code>
- modelchecking: <code>modelchecking/games/Hesselinkdatasize=3_nodeadlock.gm</code>
- modelchecking: <code>modelchecking/games/Hesselinkdatasize=3_property2.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=2_windowsize=4_infinitely_often_enabled_then_infinitely_often_taken.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=2_windowsize=4_infinitely_often_lost.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=2_windowsize=4_infinitely_often_receive_for_all_d.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=2_windowsize=4_invariantly_infinitely_many_reachable_taus.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=2_windowsize=4_no_duplication_of_messages.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=2_windowsize=4_read_then_eventually_send.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=2_windowsize=4_read_then_eventually_send_if_fair.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=4_windowsize=3_infinitely_often_read_write.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=4_windowsize=3_infinitely_often_receive_d1.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=4_windowsize=3_invariantly_infinitely_many_reachable_taus.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=4_windowsize=3_no_generation_of_messages.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=4_windowsize=3_nodeadlock.gm</code>
- modelchecking: <code>modelchecking/games/SWPdatasize=4_windowsize=3_read_then_eventually_send_if_fair.gm</code>
- pgsolver: <code>pgsolver/games/cliquegame(10000).gm</code>

6 games are invalid PGSolver input. Each ends with a stray `timeout: WARNING: Wait(N) failed: No child processes` line leaked from the generator:

- `mlsolver/games/LTMucalcBinaryCountern=2.gm`
- `mlsolver/games/Petrin=3_compact.gm`
- `mlsolver/games/Petrin=4.gm`
- `pgsolver/games/steadygame(1000,_1,_20,_1,_20)id=10.gm`
- `pgsolver/games/steadygame(1000,_1,_20,_1,_20)id=5.gm`
- `pgsolver/games/steadygame(1000,_1,_20,_1,_20)id=6.gm`

`FLCTLStarLimitClosure n=1` (plain and compact) has a likely error in the reference data. Every complete backend and upstream [Oink](https://github.com/trolando/oink) (`--zlk`) agree that Even wins start vertex 0, with identical Even / Odd winner counts: 8,276 / 1,309 for the compact game and 148,284 / 26,383 for the plain game. `data.yaml` records pbespgsolve's answer as false for all reductions.

115 pgsolver games (cliquegame, modelcheckerladder, recursiveladder, steadygame) exceed the recursion depth limit of 1024 in `zielonka`, `zielonka-sym`, `lifting`, `lifting-sym` and `lifting-adaptive`, including `+iter`. They have between 1,502 and 150,002 distinct priorities. With `+pre`, compression gets 103 games past the guard. The remaining 5 modelcheckerladder and 7 recursiveladder games stay refused because alternating parities leave nothing to merge.

## Columns and metrics

| Column | Meaning |
| --- | --- |
| archive, game | Source ZIP name and member path, with the .bz2 suffix removed. (archive listing) records a failed ZIP directory download. |
| backend | DECOMP solver name. +pre compresses priorities and repeatedly removes simple winning regions and their attractors; +iter removes certified dominions and their attractors between solver rounds. |
| outcome | complete assigns every vertex; partial leaves vertices unresolved; host limit exceeds the allowed host-tree size; timeout exceeds the deadline; out of memory reports allocation failure or a signal with evidence of the cap; crash records another signal; error records another exit or parsing failure; invalid input is rejected PGSolver input; download failed exhausts network/disk retries. |
| vertices, edges | Game vertices (final declarations, including replacements) and successor entries. |
| distinct_priorities, maximum_priority | Number of distinct input priorities and the greatest input priority, before compression. |
| won_even, won_odd (Won E / O) | Certified winning vertices, including all shaving rounds and attracted vertices, once each. |
| parameters_even, parameters_odd | Final search bounds: lifting uses `H` (host height), `K` (Strahler bound), `t` (size allowance); `pm-strahler` uses `k` (Strahler bound), `t` (size allowance), `depth` (number of measure levels); `pm-succinct` uses `bits` (bit budget per measure) and `depth`. Zielonka methods have no search parameters. |
| seconds | Wall seconds for a backend, including launch, solver execution and metric parsing. Downloading and shared game-metadata parsing are excluded. |
| peak_rss_mib | Maximum individual child peak RSS, measured with os.wait4 for the solver and metric parser; MiB is 2^20 bytes. It is not a cumulative RUSAGE_CHILDREN reading. |
| detail | Short error, signal or unavailable-metadata explanation. Older non-complete +pre rows with only preprocessing statistics show (error text not recorded); the CSV is unchanged. |
| start_winner | even or odd for the declared start vertex (default 0), including shaved winners; unresolved if uncertified or the vertex is absent. Blank in older pilot rows. |
| reference_solution, reference_agrees | Original reference Boolean and true/false agreement with a certified start winner; blank means unchecked. |
| winner_disagrees | true marks complete rows when complete backends disagree on winning counts or known start winners; false means no disagreement found; blank means incomplete. |
| timeout_seconds, memory_gib, jobs, max_host_leaves, machine | Recorded execution limits and machine for each row; memory_gib is in 2^30 bytes. |
| solver_build | First line of pg2adot --version, with Meson build type and optimization when available. Empty for older rows until explicitly backfilled. |

Paired values use Even / Odd (E / O); CSV columns use _even and _odd. A hyphen in the tables or an empty CSV field means unavailable. Tree metrics cover the remaining game (after preprocessing and earlier shaving rounds): its decomposition or occupied progress-measure tree, including its extra root. Empty trees have zero metrics. Connector boxes, shaved regions and unresolved vertices do not count. Parameters and trees describe the final round; winning counts include all rounds.

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

## Winner checks

| Archive | Winner disagreements | Reference mismatches |
| --- | --- | --- |
| mlsolver | 0 | 27 |
| pgsolver | 0 | 0 |
| equivchecking-hesselink | 0 | 0 |
| modelchecking | 0 | 0 |
| equivchecking | 0 | 0 |

Full lists of affected games and backends are on the archive pages linked below.

## Summary

Counts are backend runs. Archive-listing failures have no backend and do not count as games.

| Archive | Backend | Games | complete | partial | host limit | timeout | out of memory | crash | error | invalid input | download failed |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| mlsolver | lifting | 192 | 110 | 0 | 0 | 43 | 36 | 0 | 0 | 3 | 0 |
| mlsolver | lifting+pre | 192 | 132 | 0 | 0 | 29 | 28 | 0 | 0 | 3 | 0 |
| mlsolver | lifting+iter | 192 | 110 | 0 | 0 | 43 | 36 | 0 | 0 | 3 | 0 |
| mlsolver | lifting+pre+iter | 192 | 132 | 0 | 0 | 29 | 28 | 0 | 0 | 3 | 0 |
| mlsolver | lifting-adaptive | 192 | 170 | 0 | 0 | 19 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | lifting-adaptive+pre | 192 | 174 | 0 | 0 | 15 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | lifting-adaptive+iter | 192 | 173 | 0 | 0 | 16 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | lifting-adaptive+pre+iter | 192 | 176 | 0 | 0 | 13 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | lifting-sym | 192 | 125 | 0 | 0 | 28 | 36 | 0 | 0 | 3 | 0 |
| mlsolver | lifting-sym+pre | 192 | 145 | 0 | 0 | 16 | 28 | 0 | 0 | 3 | 0 |
| mlsolver | lifting-sym+iter | 192 | 126 | 0 | 0 | 27 | 36 | 0 | 0 | 3 | 0 |
| mlsolver | lifting-sym+pre+iter | 192 | 145 | 0 | 0 | 16 | 28 | 0 | 0 | 3 | 0 |
| mlsolver | pm-strahler | 192 | 172 | 0 | 0 | 17 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | pm-strahler+pre | 192 | 179 | 0 | 0 | 10 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | pm-strahler+iter | 192 | 182 | 0 | 0 | 7 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | pm-strahler+pre+iter | 192 | 182 | 0 | 0 | 7 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | pm-succinct | 192 | 176 | 0 | 0 | 13 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | pm-succinct+pre | 192 | 180 | 0 | 0 | 9 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | pm-succinct+iter | 192 | 185 | 0 | 0 | 4 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | pm-succinct+pre+iter | 192 | 185 | 0 | 0 | 4 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | zielonka | 192 | 188 | 0 | 0 | 1 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | zielonka+pre | 192 | 189 | 0 | 0 | 0 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | zielonka-sym | 192 | 187 | 0 | 0 | 2 | 0 | 0 | 0 | 3 | 0 |
| mlsolver | zielonka-sym+pre | 192 | 189 | 0 | 0 | 0 | 0 | 0 | 0 | 3 | 0 |
| pgsolver | lifting | 316 | 13 | 0 | 0 | 38 | 147 | 0 | 115 | 3 | 0 |
| pgsolver | lifting+pre | 316 | 274 | 0 | 0 | 6 | 21 | 0 | 12 | 3 | 0 |
| pgsolver | lifting+iter | 316 | 13 | 0 | 0 | 38 | 147 | 0 | 115 | 3 | 0 |
| pgsolver | lifting+pre+iter | 316 | 274 | 0 | 0 | 6 | 21 | 0 | 12 | 3 | 0 |
| pgsolver | lifting-adaptive | 316 | 176 | 0 | 0 | 22 | 0 | 0 | 115 | 3 | 0 |
| pgsolver | lifting-adaptive+pre | 316 | 282 | 0 | 0 | 19 | 0 | 0 | 12 | 3 | 0 |
| pgsolver | lifting-adaptive+iter | 316 | 176 | 0 | 0 | 22 | 0 | 0 | 115 | 3 | 0 |
| pgsolver | lifting-adaptive+pre+iter | 316 | 282 | 0 | 0 | 19 | 0 | 0 | 12 | 3 | 0 |
| pgsolver | lifting-sym | 316 | 41 | 0 | 0 | 10 | 147 | 0 | 115 | 3 | 0 |
| pgsolver | lifting-sym+pre | 316 | 275 | 0 | 0 | 5 | 21 | 0 | 12 | 3 | 0 |
| pgsolver | lifting-sym+iter | 316 | 41 | 0 | 0 | 10 | 147 | 0 | 115 | 3 | 0 |
| pgsolver | lifting-sym+pre+iter | 316 | 275 | 0 | 0 | 5 | 21 | 0 | 12 | 3 | 0 |
| pgsolver | pm-strahler | 316 | 279 | 0 | 0 | 30 | 4 | 0 | 0 | 3 | 0 |
| pgsolver | pm-strahler+pre | 316 | 283 | 0 | 0 | 26 | 4 | 0 | 0 | 3 | 0 |
| pgsolver | pm-strahler+iter | 316 | 296 | 0 | 0 | 13 | 4 | 0 | 0 | 3 | 0 |
| pgsolver | pm-strahler+pre+iter | 316 | 298 | 0 | 0 | 11 | 4 | 0 | 0 | 3 | 0 |
| pgsolver | pm-succinct | 316 | 281 | 0 | 0 | 28 | 4 | 0 | 0 | 3 | 0 |
| pgsolver | pm-succinct+pre | 316 | 285 | 0 | 0 | 24 | 4 | 0 | 0 | 3 | 0 |
| pgsolver | pm-succinct+iter | 316 | 298 | 0 | 0 | 11 | 4 | 0 | 0 | 3 | 0 |
| pgsolver | pm-succinct+pre+iter | 316 | 300 | 0 | 0 | 10 | 3 | 0 | 0 | 3 | 0 |
| pgsolver | zielonka | 316 | 183 | 0 | 0 | 15 | 0 | 0 | 115 | 3 | 0 |
| pgsolver | zielonka+pre | 316 | 286 | 0 | 0 | 15 | 0 | 0 | 12 | 3 | 0 |
| pgsolver | zielonka-sym | 316 | 181 | 0 | 0 | 17 | 0 | 0 | 115 | 3 | 0 |
| pgsolver | zielonka-sym+pre | 316 | 284 | 0 | 0 | 17 | 0 | 0 | 12 | 3 | 0 |
| equivchecking-hesselink | lifting | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting+pre | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting+pre+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting-adaptive | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting-adaptive+pre | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting-adaptive+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting-adaptive+pre+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting-sym | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting-sym+pre | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting-sym+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | lifting-sym+pre+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | pm-strahler | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | pm-strahler+pre | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | pm-strahler+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | pm-strahler+pre+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | pm-succinct | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | pm-succinct+pre | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | pm-succinct+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | pm-succinct+pre+iter | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | zielonka | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | zielonka+pre | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | zielonka-sym | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| equivchecking-hesselink | zielonka-sym+pre | 10 | 4 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 0 |
| modelchecking | lifting | 313 | 200 | 0 | 0 | 97 | 16 | 0 | 0 | 0 | 0 |
| modelchecking | lifting+pre | 313 | 264 | 0 | 0 | 40 | 9 | 0 | 0 | 0 | 0 |
| modelchecking | lifting+iter | 313 | 200 | 0 | 0 | 97 | 16 | 0 | 0 | 0 | 0 |
| modelchecking | lifting+pre+iter | 313 | 263 | 0 | 0 | 41 | 9 | 0 | 0 | 0 | 0 |
| modelchecking | lifting-adaptive | 313 | 299 | 0 | 0 | 7 | 7 | 0 | 0 | 0 | 0 |
| modelchecking | lifting-adaptive+pre | 313 | 302 | 0 | 0 | 3 | 8 | 0 | 0 | 0 | 0 |
| modelchecking | lifting-adaptive+iter | 313 | 299 | 0 | 0 | 7 | 7 | 0 | 0 | 0 | 0 |
| modelchecking | lifting-adaptive+pre+iter | 313 | 305 | 0 | 0 | 0 | 8 | 0 | 0 | 0 | 0 |
| modelchecking | lifting-sym | 313 | 220 | 0 | 0 | 77 | 16 | 0 | 0 | 0 | 0 |
| modelchecking | lifting-sym+pre | 313 | 273 | 0 | 0 | 31 | 9 | 0 | 0 | 0 | 0 |
| modelchecking | lifting-sym+iter | 313 | 221 | 0 | 0 | 76 | 16 | 0 | 0 | 0 | 0 |
| modelchecking | lifting-sym+pre+iter | 313 | 276 | 0 | 0 | 28 | 9 | 0 | 0 | 0 | 0 |
| modelchecking | pm-strahler | 313 | 279 | 0 | 0 | 23 | 11 | 0 | 0 | 0 | 0 |
| modelchecking | pm-strahler+pre | 313 | 296 | 0 | 0 | 6 | 11 | 0 | 0 | 0 | 0 |
| modelchecking | pm-strahler+iter | 313 | 299 | 0 | 0 | 3 | 11 | 0 | 0 | 0 | 0 |
| modelchecking | pm-strahler+pre+iter | 313 | 300 | 0 | 0 | 2 | 11 | 0 | 0 | 0 | 0 |
| modelchecking | pm-succinct | 313 | 287 | 0 | 0 | 15 | 11 | 0 | 0 | 0 | 0 |
| modelchecking | pm-succinct+pre | 313 | 299 | 0 | 0 | 3 | 11 | 0 | 0 | 0 | 0 |
| modelchecking | pm-succinct+iter | 313 | 301 | 0 | 0 | 1 | 11 | 0 | 0 | 0 | 0 |
| modelchecking | pm-succinct+pre+iter | 313 | 301 | 0 | 0 | 1 | 11 | 0 | 0 | 0 | 0 |
| modelchecking | zielonka | 313 | 305 | 0 | 0 | 1 | 7 | 0 | 0 | 0 | 0 |
| modelchecking | zielonka+pre | 313 | 304 | 0 | 0 | 1 | 8 | 0 | 0 | 0 | 0 |
| modelchecking | zielonka-sym | 313 | 305 | 0 | 0 | 1 | 7 | 0 | 0 | 0 | 0 |
| modelchecking | zielonka-sym+pre | 313 | 305 | 0 | 0 | 0 | 8 | 0 | 0 | 0 | 0 |
| equivchecking | lifting | 206 | 79 | 0 | 0 | 104 | 23 | 0 | 0 | 0 | 0 |
| equivchecking | lifting+pre | 206 | 94 | 0 | 0 | 90 | 22 | 0 | 0 | 0 | 0 |
| equivchecking | lifting+iter | 206 | 79 | 0 | 0 | 104 | 23 | 0 | 0 | 0 | 0 |
| equivchecking | lifting+pre+iter | 206 | 94 | 0 | 0 | 90 | 22 | 0 | 0 | 0 | 0 |
| equivchecking | lifting-adaptive | 206 | 177 | 0 | 0 | 14 | 15 | 0 | 0 | 0 | 0 |
| equivchecking | lifting-adaptive+pre | 206 | 181 | 0 | 0 | 10 | 15 | 0 | 0 | 0 | 0 |
| equivchecking | lifting-adaptive+iter | 206 | 183 | 0 | 0 | 8 | 15 | 0 | 0 | 0 | 0 |
| equivchecking | lifting-adaptive+pre+iter | 206 | 184 | 0 | 0 | 7 | 15 | 0 | 0 | 0 | 0 |
| equivchecking | lifting-sym | 206 | 120 | 0 | 0 | 63 | 23 | 0 | 0 | 0 | 0 |
| equivchecking | lifting-sym+pre | 206 | 134 | 0 | 0 | 50 | 22 | 0 | 0 | 0 | 0 |
| equivchecking | lifting-sym+iter | 206 | 121 | 0 | 0 | 62 | 23 | 0 | 0 | 0 | 0 |
| equivchecking | lifting-sym+pre+iter | 206 | 140 | 0 | 0 | 44 | 22 | 0 | 0 | 0 | 0 |
| equivchecking | pm-strahler | 206 | 174 | 0 | 0 | 11 | 21 | 0 | 0 | 0 | 0 |
| equivchecking | pm-strahler+pre | 206 | 177 | 0 | 0 | 8 | 21 | 0 | 0 | 0 | 0 |
| equivchecking | pm-strahler+iter | 206 | 181 | 0 | 0 | 4 | 21 | 0 | 0 | 0 | 0 |
| equivchecking | pm-strahler+pre+iter | 206 | 181 | 0 | 0 | 5 | 20 | 0 | 0 | 0 | 0 |
| equivchecking | pm-succinct | 206 | 175 | 0 | 0 | 13 | 18 | 0 | 0 | 0 | 0 |
| equivchecking | pm-succinct+pre | 206 | 180 | 0 | 0 | 8 | 18 | 0 | 0 | 0 | 0 |
| equivchecking | pm-succinct+iter | 206 | 181 | 0 | 0 | 6 | 19 | 0 | 0 | 0 | 0 |
| equivchecking | pm-succinct+pre+iter | 206 | 181 | 0 | 0 | 7 | 18 | 0 | 0 | 0 | 0 |
| equivchecking | zielonka | 206 | 189 | 0 | 0 | 2 | 15 | 0 | 0 | 0 | 0 |
| equivchecking | zielonka+pre | 206 | 190 | 0 | 0 | 1 | 15 | 0 | 0 | 0 | 0 |
| equivchecking | zielonka-sym | 206 | 186 | 0 | 0 | 5 | 15 | 0 | 0 | 0 | 0 |
| equivchecking | zielonka-sym+pre | 206 | 186 | 0 | 0 | 5 | 15 | 0 | 0 | 0 | 0 |

Tree statistics use complete runs only, including zero-sized trees. Each cell is median / maximum; E and O are separate columns.

| Archive | Backend | Complete | nodes E | nodes O | leaves E | leaves O | height E | height O | width E | width O | strahler E | strahler O |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| mlsolver | lifting | 110 | 2 / 6 | 0 / 5 | 1 / 2 | 0 / 1 | 2 / 5 | 0 / 5 | 1 / 2 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting+pre | 132 | 2 / 6 | 0 / 1 | 1 / 2 | 0 / 1 | 2 / 4 | 0 / 1 | 1 / 2 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting+iter | 110 | 2 / 6 | 0 / 5 | 1 / 2 | 0 / 1 | 2 / 5 | 0 / 5 | 1 / 2 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting+pre+iter | 132 | 2 / 6 | 0 / 1 | 1 / 2 | 0 / 1 | 2 / 4 | 0 / 1 | 1 / 2 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting-adaptive | 170 | 3 / 31 | 0 / 7 | 1 / 10 | 0 / 1 | 2 / 10 | 0 / 7 | 1 / 9 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting-adaptive+pre | 174 | 2 / 30 | 0 / 6 | 1 / 12 | 0 / 1 | 2 / 7 | 0 / 6 | 1 / 10 | 0 / 1 | 1 / 3 | 0 / 1 |
| mlsolver | lifting-adaptive+iter | 173 | 2 / 28 | 0 / 2 | 1 / 10 | 0 / 1 | 2 / 9 | 0 / 2 | 1 / 9 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting-adaptive+pre+iter | 176 | 2 / 25 | 0 / 1 | 1 / 9 | 0 / 1 | 2 / 6 | 0 / 1 | 1 / 8 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting-sym | 125 | 2 / 9 | 0 / 5 | 1 / 2 | 0 / 1 | 2 / 9 | 0 / 5 | 1 / 2 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting-sym+pre | 145 | 2 / 14 | 0 / 1 | 1 / 5 | 0 / 1 | 2 / 5 | 0 / 1 | 1 / 4 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting-sym+iter | 126 | 2 / 9 | 0 / 5 | 1 / 2 | 0 / 1 | 2 / 9 | 0 / 5 | 1 / 2 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | lifting-sym+pre+iter | 145 | 2 / 14 | 0 / 1 | 1 / 5 | 0 / 1 | 2 / 5 | 0 / 1 | 1 / 4 | 0 / 1 | 1 / 2 | 0 / 1 |
| mlsolver | pm-strahler | 172 | 15 / 7059 | 0 / 83 | 7 / 2331 | 0 / 32 | 2 / 14 | 0 / 9 | 7 / 2331 | 0 / 32 | 2 / 4 | 0 / 2 |
| mlsolver | pm-strahler+pre | 179 | 4 / 6825 | 0 / 749 | 3 / 2624 | 0 / 131 | 2 / 10 | 0 / 9 | 3 / 2624 | 0 / 131 | 2 / 5 | 0 / 3 |
| mlsolver | pm-strahler+iter | 182 | 2 / 7919 | 0 / 3 | 1 / 2300 | 0 / 1 | 2 / 14 | 0 / 3 | 1 / 2300 | 0 / 1 | 1 / 4 | 0 / 1 |
| mlsolver | pm-strahler+pre+iter | 182 | 2 / 4173 | 0 / 3 | 1 / 1875 | 0 / 1 | 2 / 10 | 0 / 3 | 1 / 1875 | 0 / 1 | 1 / 4 | 0 / 1 |
| mlsolver | pm-succinct | 176 | 17 / 5817 | 0 / 1012 | 7.5 / 1955 | 0 / 159 | 2 / 14 | 0 / 10 | 7.5 / 1955 | 0 / 159 | 2 / 5 | 0 / 4 |
| mlsolver | pm-succinct+pre | 180 | 4 / 6578 | 0 / 853 | 3 / 2195 | 0 / 159 | 2 / 10 | 0 / 9 | 3 / 2195 | 0 / 159 | 2 / 6 | 0 / 4 |
| mlsolver | pm-succinct+iter | 185 | 4 / 22580 | 0 / 908 | 3 / 7178 | 0 / 189 | 2 / 14 | 0 / 10 | 3 / 7178 | 0 / 189 | 2 / 6 | 0 / 4 |
| mlsolver | pm-succinct+pre+iter | 185 | 4 / 17004 | 0 / 719 | 3 / 7922 | 0 / 189 | 2 / 10 | 0 / 9 | 3 / 7922 | 0 / 189 | 2 / 6 | 0 / 4 |
| mlsolver | zielonka | 188 | 3 / 251 | 0 / 11 | 1 / 106 | 0 / 1 | 2 / 14 | 0 / 11 | 1 / 58 | 0 / 1 | 1 / 4 | 0 / 1 |
| mlsolver | zielonka+pre | 189 | 2 / 244 | 0 / 6 | 1 / 105 | 0 / 1 | 2 / 9 | 0 / 6 | 1 / 57 | 0 / 1 | 1 / 4 | 0 / 1 |
| mlsolver | zielonka-sym | 187 | 3 / 113 | 0 / 10 | 1 / 47 | 0 / 1 | 2 / 14 | 0 / 10 | 1 / 28 | 0 / 1 | 1 / 4 | 0 / 1 |
| mlsolver | zielonka-sym+pre | 189 | 2 / 244 | 0 / 6 | 1 / 105 | 0 / 1 | 2 / 9 | 0 / 6 | 1 / 57 | 0 / 1 | 1 / 4 | 0 / 1 |
| pgsolver | lifting | 13 | 2 / 3 | 1 / 2 | 1 / 2 | 1 / 1 | 2 / 2 | 1 / 2 | 1 / 2 | 1 / 1 | 1 / 2 | 1 / 1 |
| pgsolver | lifting+pre | 274 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 1 | 0 / 1 |
| pgsolver | lifting+iter | 13 | 2 / 3 | 1 / 2 | 1 / 2 | 1 / 1 | 2 / 2 | 1 / 2 | 1 / 2 | 1 / 1 | 1 / 2 | 1 / 1 |
| pgsolver | lifting+pre+iter | 274 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 1 | 0 / 1 |
| pgsolver | lifting-adaptive | 176 | 1 / 6 | 2 / 6 | 1 / 2 | 1 / 2 | 1 / 6 | 2 / 6 | 1 / 2 | 1 / 2 | 1 / 2 | 1 / 2 |
| pgsolver | lifting-adaptive+pre | 282 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 1 | 0 / 1 |
| pgsolver | lifting-adaptive+iter | 176 | 1 / 6 | 2 / 6 | 1 / 1 | 1 / 1 | 1 / 6 | 2 / 6 | 1 / 1 | 1 / 1 | 1 / 1 | 1 / 1 |
| pgsolver | lifting-adaptive+pre+iter | 282 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 1 | 0 / 1 |
| pgsolver | lifting-sym | 41 | 1 / 4 | 2 / 6 | 1 / 2 | 1 / 1 | 1 / 4 | 2 / 6 | 1 / 2 | 1 / 1 | 1 / 2 | 1 / 1 |
| pgsolver | lifting-sym+pre | 275 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 1 | 0 / 1 |
| pgsolver | lifting-sym+iter | 41 | 1 / 4 | 2 / 6 | 1 / 2 | 1 / 1 | 1 / 4 | 2 / 6 | 1 / 2 | 1 / 1 | 1 / 2 | 1 / 1 |
| pgsolver | lifting-sym+pre+iter | 275 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 1 | 0 / 1 | 0 / 1 | 0 / 1 |
| pgsolver | pm-strahler | 279 | 39 / 876501 | 47 / 125000 | 4 / 1001 | 5 / 301 | 6 / 25001 | 7 / 25001 | 4 / 1001 | 5 / 301 | 2 / 2 | 2 / 3 |
| pgsolver | pm-strahler+pre | 283 | 0 / 876501 | 0 / 26454 | 0 / 1001 | 0 / 301 | 0 / 1001 | 0 / 152 | 0 / 1001 | 0 / 301 | 0 / 2 | 0 / 3 |
| pgsolver | pm-strahler+iter | 296 | 10.5 / 876501 | 7 / 25125 | 1 / 1001 | 1 / 289 | 6 / 25001 | 7 / 25001 | 1 / 1001 | 1 / 289 | 1 / 2 | 1 / 3 |
| pgsolver | pm-strahler+pre+iter | 298 | 0 / 876501 | 0 / 24836 | 0 / 1001 | 0 / 289 | 0 / 1001 | 0 / 152 | 0 / 1001 | 0 / 289 | 0 / 2 | 0 / 3 |
| pgsolver | pm-succinct | 281 | 49 / 751501 | 61 / 124995 | 5 / 1001 | 5 / 501 | 6 / 25001 | 7 / 25001 | 5 / 1001 | 5 / 501 | 2 / 3 | 2 / 3 |
| pgsolver | pm-succinct+pre | 285 | 0 / 751501 | 0 / 86453 | 0 / 1001 | 0 / 501 | 0 / 1001 | 0 / 302 | 0 / 1001 | 0 / 501 | 0 / 2 | 0 / 3 |
| pgsolver | pm-succinct+iter | 298 | 32 / 751501 | 40 / 124995 | 5 / 1001 | 5 / 12 | 6 / 25001 | 7 / 25001 | 5 / 1001 | 5 / 12 | 2 / 2 | 2 / 3 |
| pgsolver | pm-succinct+pre+iter | 300 | 0 / 751501 | 0 / 777 | 0 / 1001 | 0 / 12 | 0 / 1001 | 0 / 752 | 0 / 1001 | 0 / 12 | 0 / 2 | 0 / 3 |
| pgsolver | zielonka | 183 | 1 / 5001 | 2 / 101 | 1 / 4951 | 1 / 2 | 1 / 101 | 2 / 101 | 1 / 100 | 1 / 2 | 1 / 2 | 1 / 2 |
| pgsolver | zielonka+pre | 286 | 0 / 4951 | 0 / 101 | 0 / 4901 | 0 / 1 | 0 / 101 | 0 / 101 | 0 / 99 | 0 / 1 | 0 / 2 | 0 / 1 |
| pgsolver | zielonka-sym | 181 | 1 / 2501 | 2 / 51 | 1 / 2451 | 1 / 2 | 1 / 51 | 2 / 51 | 1 / 50 | 1 / 2 | 1 / 2 | 1 / 2 |
| pgsolver | zielonka-sym+pre | 284 | 0 / 2451 | 0 / 51 | 0 / 2401 | 0 / 1 | 0 / 51 | 0 / 51 | 0 / 49 | 0 / 1 | 0 / 2 | 0 / 1 |
| equivchecking-hesselink | lifting | 4 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 1 / 1 |
| equivchecking-hesselink | lifting+pre | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | lifting+iter | 4 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 1 / 1 |
| equivchecking-hesselink | lifting+pre+iter | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | lifting-adaptive | 4 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 1 / 1 |
| equivchecking-hesselink | lifting-adaptive+pre | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | lifting-adaptive+iter | 4 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 1 / 1 |
| equivchecking-hesselink | lifting-adaptive+pre+iter | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | lifting-sym | 4 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 1 / 1 |
| equivchecking-hesselink | lifting-sym+pre | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | lifting-sym+iter | 4 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 1 / 1 |
| equivchecking-hesselink | lifting-sym+pre+iter | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | pm-strahler | 4 | 0 / 0 | 11 / 11 | 0 / 0 | 5 / 5 | 0 / 0 | 3 / 3 | 0 / 0 | 5 / 5 | 0 / 0 | 2 / 2 |
| equivchecking-hesselink | pm-strahler+pre | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | pm-strahler+iter | 4 | 0 / 0 | 3 / 3 | 0 / 0 | 1 / 1 | 0 / 0 | 3 / 3 | 0 / 0 | 1 / 1 | 0 / 0 | 1 / 1 |
| equivchecking-hesselink | pm-strahler+pre+iter | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | pm-succinct | 4 | 0 / 0 | 11 / 11 | 0 / 0 | 5 / 5 | 0 / 0 | 3 / 3 | 0 / 0 | 5 / 5 | 0 / 0 | 2 / 2 |
| equivchecking-hesselink | pm-succinct+pre | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | pm-succinct+iter | 4 | 0 / 0 | 7 / 7 | 0 / 0 | 3 / 3 | 0 / 0 | 3 / 3 | 0 / 0 | 3 / 3 | 0 / 0 | 2 / 2 |
| equivchecking-hesselink | pm-succinct+pre+iter | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | zielonka | 4 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 1 / 1 |
| equivchecking-hesselink | zielonka+pre | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| equivchecking-hesselink | zielonka-sym | 4 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 2 / 2 | 0 / 0 | 1 / 1 | 0 / 0 | 1 / 1 |
| equivchecking-hesselink | zielonka-sym+pre | 4 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| modelchecking | lifting | 200 | 1 / 4 | 0 / 3 | 1 / 3 | 0 / 1 | 1 / 2 | 0 / 3 | 1 / 3 | 0 / 1 | 1 / 2 | 0 / 1 |
| modelchecking | lifting+pre | 264 | 0 / 4 | 0 / 2 | 0 / 3 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 3 | 0 / 1 | 0 / 2 | 0 / 1 |
| modelchecking | lifting+iter | 200 | 1 / 4 | 0 / 3 | 1 / 3 | 0 / 1 | 1 / 2 | 0 / 3 | 1 / 3 | 0 / 1 | 1 / 2 | 0 / 1 |
| modelchecking | lifting+pre+iter | 263 | 0 / 4 | 0 / 2 | 0 / 3 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 3 | 0 / 1 | 0 / 2 | 0 / 1 |
| modelchecking | lifting-adaptive | 299 | 1 / 8 | 0 / 3 | 1 / 7 | 0 / 1 | 1 / 2 | 0 / 3 | 1 / 7 | 0 / 1 | 1 / 2 | 0 / 1 |
| modelchecking | lifting-adaptive+pre | 302 | 0 / 8 | 0 / 2 | 0 / 7 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 7 | 0 / 1 | 0 / 2 | 0 / 1 |
| modelchecking | lifting-adaptive+iter | 299 | 1 / 8 | 0 / 3 | 1 / 7 | 0 / 1 | 1 / 2 | 0 / 3 | 1 / 7 | 0 / 1 | 1 / 2 | 0 / 1 |
| modelchecking | lifting-adaptive+pre+iter | 305 | 0 / 8 | 0 / 2 | 0 / 7 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 7 | 0 / 1 | 0 / 2 | 0 / 1 |
| modelchecking | lifting-sym | 220 | 1 / 4 | 0 / 3 | 1 / 3 | 0 / 1 | 1 / 2 | 0 / 3 | 1 / 3 | 0 / 1 | 1 / 2 | 0 / 1 |
| modelchecking | lifting-sym+pre | 273 | 0 / 4 | 0 / 2 | 0 / 3 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 3 | 0 / 1 | 0 / 2 | 0 / 1 |
| modelchecking | lifting-sym+iter | 221 | 1 / 4 | 0 / 3 | 1 / 3 | 0 / 1 | 1 / 2 | 0 / 3 | 1 / 3 | 0 / 1 | 1 / 2 | 0 / 1 |
| modelchecking | lifting-sym+pre+iter | 276 | 0 / 4 | 0 / 2 | 0 / 3 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 3 | 0 / 1 | 0 / 2 | 0 / 1 |
| modelchecking | pm-strahler | 279 | 1 / 515 | 0 / 190 | 1 / 514 | 0 / 68 | 1 / 3 | 0 / 4 | 1 / 514 | 0 / 68 | 1 / 2 | 0 / 2 |
| modelchecking | pm-strahler+pre | 296 | 0 / 14 | 0 / 55 | 0 / 13 | 0 / 19 | 0 / 3 | 0 / 4 | 0 / 13 | 0 / 19 | 0 / 2 | 0 / 2 |
| modelchecking | pm-strahler+iter | 299 | 2 / 33 | 0 / 25 | 1 / 19 | 0 / 19 | 2 / 3 | 0 / 4 | 1 / 19 | 0 / 19 | 1 / 2 | 0 / 2 |
| modelchecking | pm-strahler+pre+iter | 300 | 0 / 14 | 0 / 25 | 0 / 13 | 0 / 19 | 0 / 3 | 0 / 4 | 0 / 13 | 0 / 19 | 0 / 2 | 0 / 2 |
| modelchecking | pm-succinct | 287 | 1 / 515 | 0 / 190 | 1 / 514 | 0 / 68 | 1 / 3 | 0 / 4 | 1 / 514 | 0 / 68 | 1 / 2 | 0 / 3 |
| modelchecking | pm-succinct+pre | 299 | 0 / 14 | 0 / 55 | 0 / 13 | 0 / 18 | 0 / 3 | 0 / 4 | 0 / 13 | 0 / 18 | 0 / 2 | 0 / 3 |
| modelchecking | pm-succinct+iter | 301 | 2 / 15 | 0 / 23 | 1 / 13 | 0 / 17 | 2 / 3 | 0 / 4 | 1 / 13 | 0 / 17 | 1 / 2 | 0 / 2 |
| modelchecking | pm-succinct+pre+iter | 301 | 0 / 14 | 0 / 23 | 0 / 13 | 0 / 17 | 0 / 3 | 0 / 4 | 0 / 13 | 0 / 17 | 0 / 2 | 0 / 2 |
| modelchecking | zielonka | 305 | 1 / 8 | 0 / 3 | 1 / 7 | 0 / 1 | 1 / 2 | 0 / 3 | 1 / 7 | 0 / 1 | 1 / 2 | 0 / 1 |
| modelchecking | zielonka+pre | 304 | 0 / 8 | 0 / 2 | 0 / 7 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 7 | 0 / 1 | 0 / 2 | 0 / 1 |
| modelchecking | zielonka-sym | 305 | 1 / 8 | 0 / 3 | 1 / 7 | 0 / 1 | 1 / 2 | 0 / 3 | 1 / 7 | 0 / 1 | 1 / 2 | 0 / 1 |
| modelchecking | zielonka-sym+pre | 305 | 0 / 8 | 0 / 2 | 0 / 7 | 0 / 1 | 0 / 2 | 0 / 2 | 0 / 7 | 0 / 1 | 0 / 2 | 0 / 1 |
| equivchecking | lifting | 79 | 0 / 1 | 2 / 3 | 0 / 1 | 1 / 2 | 0 / 1 | 2 / 2 | 0 / 1 | 1 / 2 | 0 / 1 | 1 / 2 |
| equivchecking | lifting+pre | 94 | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 |
| equivchecking | lifting+iter | 79 | 0 / 1 | 2 / 3 | 0 / 1 | 1 / 2 | 0 / 1 | 2 / 2 | 0 / 1 | 1 / 2 | 0 / 1 | 1 / 2 |
| equivchecking | lifting+pre+iter | 94 | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 | 0 / 1 | 0 / 0 |
| equivchecking | lifting-adaptive | 177 | 1 / 1 | 0 / 15 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 |
| equivchecking | lifting-adaptive+pre | 181 | 1 / 1 | 0 / 15 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 |
| equivchecking | lifting-adaptive+iter | 183 | 1 / 1 | 0 / 7 | 1 / 1 | 0 / 6 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 6 | 1 / 1 | 0 / 2 |
| equivchecking | lifting-adaptive+pre+iter | 184 | 1 / 1 | 0 / 8 | 1 / 1 | 0 / 7 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 7 | 1 / 1 | 0 / 2 |
| equivchecking | lifting-sym | 120 | 1 / 1 | 0 / 13 | 1 / 1 | 0 / 12 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 12 | 1 / 1 | 0 / 2 |
| equivchecking | lifting-sym+pre | 134 | 0 / 1 | 0 / 13 | 0 / 1 | 0 / 12 | 0 / 1 | 0 / 2 | 0 / 1 | 0 / 12 | 0 / 1 | 0 / 2 |
| equivchecking | lifting-sym+iter | 121 | 1 / 1 | 0 / 13 | 1 / 1 | 0 / 12 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 12 | 1 / 1 | 0 / 2 |
| equivchecking | lifting-sym+pre+iter | 140 | 0 / 1 | 0 / 13 | 0 / 1 | 0 / 12 | 0 / 1 | 0 / 2 | 0 / 1 | 0 / 12 | 0 / 1 | 0 / 2 |
| equivchecking | pm-strahler | 174 | 10 / 13 | 0 / 55 | 9 / 12 | 0 / 27 | 2 / 2 | 0 / 3 | 9 / 12 | 0 / 27 | 2 / 2 | 0 / 2 |
| equivchecking | pm-strahler+pre | 177 | 9 / 13 | 0 / 53 | 8 / 12 | 0 / 26 | 2 / 2 | 0 / 3 | 8 / 12 | 0 / 26 | 2 / 2 | 0 / 2 |
| equivchecking | pm-strahler+iter | 181 | 3 / 13 | 0 / 29 | 2 / 12 | 0 / 14 | 2 / 2 | 0 / 3 | 2 / 12 | 0 / 14 | 2 / 2 | 0 / 2 |
| equivchecking | pm-strahler+pre+iter | 181 | 3 / 13 | 0 / 29 | 2 / 12 | 0 / 14 | 2 / 2 | 0 / 3 | 2 / 12 | 0 / 14 | 2 / 2 | 0 / 2 |
| equivchecking | pm-succinct | 175 | 10 / 13 | 0 / 55 | 9 / 12 | 0 / 27 | 2 / 2 | 0 / 3 | 9 / 12 | 0 / 27 | 2 / 2 | 0 / 2 |
| equivchecking | pm-succinct+pre | 180 | 10 / 13 | 0 / 53 | 9 / 12 | 0 / 26 | 2 / 2 | 0 / 3 | 9 / 12 | 0 / 26 | 2 / 2 | 0 / 2 |
| equivchecking | pm-succinct+iter | 181 | 3 / 13 | 0 / 7 | 2 / 12 | 0 / 3 | 2 / 2 | 0 / 3 | 2 / 12 | 0 / 3 | 2 / 2 | 0 / 2 |
| equivchecking | pm-succinct+pre+iter | 181 | 3 / 13 | 0 / 7 | 2 / 12 | 0 / 3 | 2 / 2 | 0 / 3 | 2 / 12 | 0 / 3 | 2 / 2 | 0 / 2 |
| equivchecking | zielonka | 189 | 1 / 1 | 0 / 15 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 |
| equivchecking | zielonka+pre | 190 | 1 / 1 | 0 / 15 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 |
| equivchecking | zielonka-sym | 186 | 1 / 1 | 0 / 15 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 |
| equivchecking | zielonka-sym+pre | 186 | 1 / 1 | 0 / 15 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 | 1 / 1 | 0 / 14 | 1 / 1 | 0 / 2 |

## Inventory

- [mlsolver, part 1](keiren/mlsolver-1.md)
- [mlsolver, part 2](keiren/mlsolver-2.md)
- [mlsolver, part 3](keiren/mlsolver-3.md)
- [mlsolver, part 4](keiren/mlsolver-4.md)
- [mlsolver, part 5](keiren/mlsolver-5.md)
- [pgsolver, part 1](keiren/pgsolver-1.md)
- [pgsolver, part 2](keiren/pgsolver-2.md)
- [pgsolver, part 3](keiren/pgsolver-3.md)
- [pgsolver, part 4](keiren/pgsolver-4.md)
- [pgsolver, part 5](keiren/pgsolver-5.md)
- [pgsolver, part 6](keiren/pgsolver-6.md)
- [pgsolver, part 7](keiren/pgsolver-7.md)
- [pgsolver, part 8](keiren/pgsolver-8.md)
- [equivchecking-hesselink, part 1](keiren/equivchecking-hesselink-1.md)
- [modelchecking, part 1](keiren/modelchecking-1.md)
- [modelchecking, part 2](keiren/modelchecking-2.md)
- [modelchecking, part 3](keiren/modelchecking-3.md)
- [modelchecking, part 4](keiren/modelchecking-4.md)
- [modelchecking, part 5](keiren/modelchecking-5.md)
- [modelchecking, part 6](keiren/modelchecking-6.md)
- [modelchecking, part 7](keiren/modelchecking-7.md)
- [equivchecking, part 1](keiren/equivchecking-1.md)
- [equivchecking, part 2](keiren/equivchecking-2.md)
- [equivchecking, part 3](keiren/equivchecking-3.md)
- [equivchecking, part 4](keiren/equivchecking-4.md)
- [equivchecking, part 5](keiren/equivchecking-5.md)
