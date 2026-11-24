# Host-order counterexamples

Both games are won entirely by Even. Fresh fixed-host runs, without shaving,
give the following numbers of certified vertices:

| Game | Vertices | AD `(3,2)` | AD `(4,3)` | STRPM `(3,2)` | STRPM `(4,3)` |
| --- | ---: | ---: | ---: | ---: | ---: |
| [ad-nonmonotone.pg](ad-nonmonotone.pg) | 70 | 70 | 0 | 0 | 0 |
| [strpm-nonmonotone.pg](strpm-nonmonotone.pg) | 44 | 44 | 44 | 44 | 22 |

Uncertified vertices remain unresolved. Increasing `k` changes the host's
branching structure, so increasing both parameters need not preserve a solve.
The STRPM result holds for both scalar and SIMD kernels.

The AD construction is explained in [incremental-t.md](../docs/incremental-t.md).
The PM game encodes the same 16-leaf obstruction through strict prefix
comparisons between adjacent leaves and equality of their shared prefixes.
