# Fixed-host examples

Both games are won entirely by Even. Fresh fixed-host runs, without shaving,
give the following numbers of certified vertices:

| Game | Vertices | AD `(3,2)` | AD `(4,3)` | STRPM `(3,2)` | STRPM `(4,3)` |
| --- | ---: | ---: | ---: | ---: | ---: |
| [ad-nonmonotone.pg](ad-nonmonotone.pg) | 70 | 70 | 0 | 0 | 0 |
| [strpm-nonmonotone.pg](strpm-nonmonotone.pg) | 44 | 44 | 44 | 44 | 22 |

Uncertified vertices remain unresolved. Increasing `k` changes the host's
branching structure, so increasing both parameters need not preserve a solve.
The STRPM result holds for both scalar and SIMD kernels.
These fixed-host fixtures are tested directly and excluded from the default
decomposition gallery.

The AD construction is explained in [incremental-t.md](../docs/incremental-t.md).
The PM game encodes the same 16-leaf obstruction through strict prefix
comparisons between adjacent leaves and equality of their shared prefixes.

## Nonstrict STRPM lifts

[strpm-nonstrict-tail.pg](strpm-nonstrict-tail.pg) has 32 vertices: Even wins
0–30 and Odd wins 31. Both `(2,1)` and `(2,2)` admit complete bounded progress
measures. The former Oink-style scalar and SIMD kernels solve `(2,1)` but leave
25 vertices unresolved at `(2,2)`, even with fixed-pair iterative shaving.
Nonstrict lifts copied unnecessary lower-priority tails from a successor.
Resetting those tails to the first canonical completion recovers the least
measure and solves both hosts. The regression checks the winners and labels
in both backends.
