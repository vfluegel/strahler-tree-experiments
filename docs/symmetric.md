# Symmetric recursion: corrected entry condition

The implementation corrects Lemma 8.2.1, item 4 (thesis printed page 141).
The printed condition fails on the two-vertex game below. The correction
applies to Algorithms 8 and 9, with the players exchanged at each recursive call.

In a call `solve(G, P, a, b)`, the lower player P has node a at level h; the
higher player Q has node b at level h+1. Write

- `C_P(a) = {v : a <= D_P(v) < a^S}` for the core, excluding its side;
- `S_P(a) = {v : D_P(v) = a^S}` for the side fibre, including TOP at a root.

Let Gᵢ be the subgame in the caller's sequence G₁ ⊇ G₂ ⊇ … after earlier
children's attractors have been removed, in which this call's subgame was
selected (the whole active domain for the initial call). The corrected entry
condition is

```text
V(G) = Gᵢ ∩ C_P(a) ∩ (C_Q(b) ∪ S_Q(b)).
```

Thus each entry label is in `[a, a^S)` for P and `[b, b^S]` for Q. G must be
total and, for a recursive child, a trap for the caller's lower player in Gᵢ.
Keep the lemma's other entry requirements: `D_P <= A_P` and `D_Q <= A_Q`
pointwise, where A is the respective least valid labelling on the original
active domain, and the two boundary conditions excluding a forced visit to
earlier labels before crossing the corresponding side boundary (items 2 to 3,
with the players swapped when necessary). Mere membership in these intervals
does not imply the upper bounds or the boundary conditions.

For `0 1 0 0; 1 0 0 1,0;`, the initial Odd call has h=1. Its top attractor
is `{0}`. Vertex 1 is outside this attractor, so moving its Odd top label
puts it at the Odd root's side, TOP. It still belongs to the Even child core
at level 0. The Even recursive call therefore receives `{1}` with its higher
(Odd) label at TOP. The printed intersection of two cores is false here;
the corrected condition includes this call. Even wins vertex 1 by staying
on its priority-0 loop, while Odd wins vertex 0.

With `--trace=FILE`, the `call` for Even's child has Odd's path at the root,
and the preceding Odd `labels` event places vertex 1 in that root's S fibre.
This is the corrected entry condition in the trace: S at a root means TOP.
The subsequent `check` events record the algorithm's own restricted-labelling
tests (paper Algorithm 2, entry lines 1 and 4, and the G_{i+1} subgame test).
They do not rerun or change the recursion.

## Proof sketch

Initially both labellings are the least priority-compatible ones. The higher
root has an empty highest-priority target; moving its unsupported top fibre
places vertices in its children or side. This preserves `D <= A` by the same
Move argument as Proposition 7.2.3 and establishes the entry intervals.

Consider an iteration at lower node a and higher child c. Moving the lower
top fibre outside the top attractor can reach `a^S`, even before the recursive
call. Thus the next call, whose lower node is c and higher node is a, must
allow `S_P(a)`. Removing the top-attractor vertices from `C_Q(c)` by Set gives
the candidate U. Old labels can leave U with escaping edges, so the code uses

```text
B = Attr_P^Gᵢ(Gᵢ \ U),     U' = U \ B = Gᵢ \ B.
```

The complement of an attractor is a P-trap and induces a total subgame. Set
moves `B ∩ U` to `c^S`, so the updated c-core in Gᵢ is exactly U'. Meanwhile
the caller's labels remain in its a-core or a-side. This gives the corrected
entry equality after swapping players. This closure is an added operation.

For the upper bound, the loop's frontier invariant says that every vertex still
in Gᵢ has Q label at least c. This holds for the first child after the higher
top fibre is moved; after each iteration, Move advances the preceding child's
side fibre outside the returned attractor. Thus `Gᵢ \ U` has Q labels
at least `c^S`, not earlier labels. Since `D_Q <= A_Q`, those targets are
outside the least Q c-core. The entry trap and earlier-label boundary conditions
allow its Q-strategy to stay in that core within Gᵢ, so P cannot attract any
of its vertices to these targets. Thus `B ∩ U` is outside that least core,
and setting it to `c^S` stays below A_Q. This is Proposition 7.2.6's exclusion
argument; it also justifies the Sets of top-attractor vertices and of vertices
attracted to the returned separator.

Induction now uses the corrected entry condition for each nonempty child.
Move advances only fibres outside their required attractors, preserving
`D <= A` by Proposition 7.2.3. The return update sets the opponent's labels on
G_{i+1} to its side, as in the winning entry case. A vertex already on that side
needs no core witness for that player; it cannot make `is_ad` succeed there.
These facts preserve the upper bounds needed by the separator argument.
Continuing short lifts from `D <= A` then reaches A, as described in §8.3.

## Regression and debug checks

`higher_side_entry` in `tests/test_ad_sym.c` checks this two-vertex game for
both algorithms, including exactly two calls and exactly one call containing
a higher-side entry (`ADSymStats.higher_side_calls`), as well as the winners.
At every entry, debug assertions check totality and both label intervals.

Before certification, debug builds independently run ordinary lifting from
the least compatible labels and assert `D_P(v) <= A_P(v)` for every active
vertex and both players. The check uses raw labels before certification can
repair them. The independent run is excluded from solver statistics.
To keep this check cheap, it is limited to games with at most 12 vertices
and hosts with at most 4096 nodes; it covers the boundary regression and the
small random-game suite. Larger games rely on the argument above and final
certificate verification. Defining NDEBUG omits these assertions and debug runs.

## Thesis correspondence

`src/ad_sym.c` shares the player-swapped recursion for Algorithms 8 and 9:
`is_ad` implements Proposition 7.2.1; `assign`/`move` implement Proposition
7.2.3's ⊕/smallMove; `set_side` implements Remark 5. `solve` follows Algorithm
8 lines 2 to 26 / Algorithm 9 lines 2 to 24. Algorithm 8's loop stops on a valid
subgame G_{i+1}; Algorithm 9 exhausts the supplied children. Empty subgames never
recurse. `ad_lift_solve.c` shares host selection; `ad_lift_continue` certifies
Algorithm 9's final labels without resetting their progress. The original
`ad_lift.c` computes Algorithm 5's least decomposition using Proposition 7.2.1
short lifts in place of Algorithm 5's recursive restart schedule.

The corrected condition allows the higher player's side fibre. The printed
intersection of both cores fails on
`0 1 0 0; 1 0 0 1,0;`. See [the entry invariant and proof sketch](#proof-sketch)
for the precise condition, trap closure, and the debug check that raw labels stay
below the independently computed least valid labelling before certification.

| Thesis correspondence | Classification |
|---|---|
| Algorithm 8 line 27 returns updated G_{i+1}, not Gᵢ | Indexing erratum. |
| Proposition 7.2.3's T/S unions preserve T/S, not H | Typographical errata. |
| Lemma 8.2.1 item 4 includes the higher player's side | Proposed invariant erratum; justified in the linked proof sketch. |
| Move the initial higher root's unsupported top labels | Faithful reading / normalization: its highest-priority target is empty. |
| Set the opponent's labels on G_{i+1} to the side on return | Faithful bookkeeping addition: the winning entry case's update. |
| Close a stale child by removing the escaping player's attractor | Added algorithmic operation; not a typographical correction. |
| Finish Algorithm 9 with asymmetric short lifts | Thesis §8.3 extension to obtain certificates, with polynomial additional cost. |

All updates affect only the current subgame and increase labels monotonically.
Attractor statistics include validity checks and certification, exclude the final
verifiers, and count ordinary and reach-through-safe computations alike.
