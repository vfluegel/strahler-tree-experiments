# Trying smaller hosts first

We counted the leaves of every Strahler host with **k=1…6, t=0…6 and
h=1…1000**, keeping the valid cases k≤h. That gives 41,895 exact
counts in [sizes.csv](experimental/host-order/sizes.csv). The count comes from
the same U/V recurrence as `genstree`, without constructing the trees. The
largest has 3,557,321,882,121,489,343 leaves, so the old capped 32-bit count would
lose distinctions that matter here.

The idea is straightforward: at each height, try the hosts in increasing leaf
count, breaking ties by k and then t. Lexicographic search spends its time
allowance on t before reconsidering k. Ratio search balances the two numbers,
but cannot see that the same pair can describe a much larger tree at another
height. The table knows those sizes.

Here is one way to compare the orders: among pairs of hosts with different
sizes, how often does the search put the larger one first? Lower is better
for this particular objective; size order itself scores zero. This comparison
uses t≥1 for all methods, since the existing ratio search excludes t=0, and
reproduces its bounded heap's tie-breaking.

| Height h | Ratio: larger first | Lexicographic: larger first |
| ---: | ---: | ---: |
| 6 | 60.5% | 23.2% |
| 10 | 53.8% | 9.1% |
| 100 | 49.3% | 1.9% |
| 1000 | 47.8% | 0.5% |

At large heights, lexicographic order is already close to size order; ratio
still puts the larger host first in almost half of comparable pairs. The
benefit of a table is knowing the order at each height, rather than guessing
from the parameter values.

Height changes the answer. There are **43 distinct consecutive
orders**, with the last change at **h=558**. Stopping at 100 would
miss 9 later changes. Up to 1000, most adjacent
heights share exactly the same order, so the code stores only the boundaries
where an order changes. This is a small lookup, even though the CSV covers the
whole range.

This is an order by host size, not a predicted speedup. AD builds these trees;
STRPM represents them implicitly and benefits from its SIMD kernel. Graph
structure, lifting and verification still decide the running time. Nor does a
larger leaf count mean one host contains another: changing k can remove useful
branching shapes, as the [checked counterexample](docs/incremental-t.md)
shows. Leaf count alone gives no solve guarantee.

`--search=table` uses this bounded order, then continues in ratio order when
larger parameters are needed. Heights beyond 1000 also use the ratio fallback.
Both players follow one order selected from their larger initial height;
their own height bounds remain intact. The order stays fixed while shaving
shrinks the game. We keep the zero-budget pairs as well, including one-vertex
games. The existing ratio and lexicographic modes remain available for direct
comparisons.

`--search=monotone-tab` reads a precomputed nested prefix. Offline generation
starts from the chain and keeps a larger host when the [embedding bound](docs/host-monotonicity.md)
proves inclusion: keep k and raise t, or raise k with T≥t+k−1. The table stores
variants for each parameter cap, sharing 31 distinct prefixes.
Runtime only looks up and reads the selected prefix. Both table modes then
continue with the ordinary ratio traversal, skipping pairs already tried.
The inclusion guarantee applies to the monotone prefix; ratio remains the
usual unrestricted fallback.

Regenerate with `python3 scripts/gen_host_order.py build/genstree`; add `--check`
to check the saved CSV, analysis and native table against the generator.
