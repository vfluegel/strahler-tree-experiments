# When larger host parameters preserve a certificate

For a fixed height h, write `U(k,t,h)` for the canonical Strahler host. The
following sufficient conditions give a rooted, level- and order-preserving
embedding into `U(K,T,h)`:

- `K = k` and `T >= t`;
- `k < K <= h` and `T >= t + k - 1`.

If `k=1` or `t=0`, the old host is a chain, so every target host of the same
height contains it. The second condition is otherwise sufficient, rather than
always minimal. It does not depend on how far K increases or on h.

Both AD and STRPM certificates transfer along this embedding. In particular,
`(3,2) -> (4,4)` preserves every certificate at the same height, whereas
`(3,2) -> (4,3)` has no such guarantee. This concerns solvability after lifting
finishes, not runtime under a time or memory limit.

## Constructing the embedding

This argument follows from the explicit labelled hosts in Definition 21 and
Corollary 23 of [Daviaud et al., *The Strahler Number of a Parity Game*](https://arxiv.org/html/2003.08627).
It is a derived sufficient condition, not a monotonicity theorem quoted from
that paper.

A leaf has h-1 bitstring components, exactly k-1 of them nonempty, using at
most t non-leading bits. Two further canonical conditions force zero strings
after exhausting that budget, and zero leading bits in an entirely nonempty
suffix.

For K>k>=2, scan a leaf from the root. Keep its first h-K empty components.
Immediately after the last of those, prefix **every** remaining component
with `0`, including the empty ones. If h=K, prefix all components from the
start. This is implemented as a research witness in
[`embed_larger_k`](../scripts/incremental_t.py).

The image has exactly h-K empty components, hence K-1 nonempty ones. Prefixing
an empty component creates a leading bit at no non-leading cost. Prefixing an
old nonempty component adds one non-leading bit. There were only k-1 such
components, so the new non-leading budget is at most t+k-1.

The canonical conditions also hold. Before the prefixed suffix, fewer than
t+k-1 non-leading bits have been used, so the new exhaustion condition cannot
apply. If the full budget is exhausted inside that suffix, all old nonempty
components and their non-leading bits have already been consumed; the
remaining components therefore map to `0`. Every string in the final
nonempty suffix begins with `0`.

The mapping depends only on the prefix already read. At a shared prefix, both
leaves either retain their next string or prefix it with `0`. Both operations
preserve the bitstring order. Thus shared prefixes, strict branching order,
and levels are preserved: this is an embedding of the whole tree.

## Why it works for both solvers

AD extends an embedding to the regular and lazy positions of its leafy host;
valid labellings transfer as explained in
[incremental-t.md](incremental-t.md).
STRPM compares priority-truncated leaf labels. The embedding preserves those
prefix comparisons, including strict progress, so its finite certificates
transfer too. Apply the condition separately to each player's actual k and h.

The guarantee also extends to fixed-pair iterative shaving. Restrict an old
round's certificate to the new residual game. Opponent wins are disjoint from
its dominion. Removing the player's own attractor cannot strand a remaining
player-owned vertex: such a vertex has no edge into that removed attractor.
Thus the restricted certificate still works, and its embedding still fits.
The earliest old round with wins left uncovered supplies progress, so the new
fixed-pair iteration cannot stall before covering the old result.

For a particular certificate with L occupied leaves and Strahler number at
most K, the universality bound also suffices: `T >= floor(log2 L)`. Taking
L=N gives a bound from the game size, often smaller than transferring the
entire old host. Neither argument justifies treating every larger `(k,t)` as
a solve.

## The height-independent bound is sharp

For t>=1 and 2<=k<h, increasing all the way to K=h really can require the
full extra k-1 budget: `U(k,t,h)` does not embed in `U(h,t+k-2,h)`.

For k=2, compare root arities. The old root has `2^(t+2)-1` children, whereas
the target at T=t has only `2^(t+1)-1`.

Inductively for k>2, the old root has two children shaped
`U(k-1,t,h-1)`, one in each outer V block. By the induction hypothesis, each
needs budget at least `t+k-2` in a full-height target child. But the target
root at T=t+k-2 has just one child with that budget; all its other children
have smaller budgets. Both old children would need the same target child,
which an embedding forbids.

Thus the uniform bound cannot be reduced. Particular heights and smaller
increases of k can admit better embeddings; this is a bound for transferring
the entire host, rather than just the certificate used by a particular game.

The unit tests check the constructive map against native generated hosts,
including chains, zero budgets and jumps to K=h. The two game counterexamples
and their solver results are in [examples/README.md](../examples/README.md).
