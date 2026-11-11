# Lifting backends

The AD fast path evaluates all occupied lazy positions together. Lower labels
are exits, equal labels are dependencies, and higher labels remain blockers for
opponent vertices. Labels stay frozen until the entire reachability closure is
finished. The original schedules, host search, certificates, lift counts and
shaving policy are unchanged. Traced runs use the original ordered scalar path.

AD selects AVX2 at runtime on x86-64, including the operating system's vector
state support. Apple Silicon uses NEON. Optional instructions live in separate
translation units; the baseline objects and wheels use no machine-specific
compiler flags. Machines without AVX2 use the fused scalar worklist. Optional
workspace allocation failures also fall back to scalar.

Set these environment variables before a solve. They are controls for testing
and measurement; normal calls need no extra options.

| `STRAHLER_AD_BACKEND` | Reachability implementation |
| --- | --- |
| unset or `auto` | Ordinary AD: fused below 64 active vertices; otherwise SIMD with eight dense sweeps followed by dirty blocks |
| `scalar` | Original per-position attractors; also disables recursive rank views |
| `fused` | Shared scalar predecessor queue |
| `packed` | Portable degree-grouped eight-vertex blocks, followed by dirty blocks |
| `dense` | SIMD blocks to convergence |
| `queue` / `queue1` | Eight / one dense sweeps, then the scalar predecessor queue |
| `dirty` / `dirty1` | Eight / one dense sweeps, then a coalesced queue of SIMD blocks |
| `byte` | Occupied-rank byte dictionary and predecessor queue; exact wider fallback above 256 symbols |

SIMD selection is always checked at runtime. Tests print an explicit skip when
AVX2 is unavailable. The portable packed path remains selectable on AVX2 and
NEON machines. Regular and TOP positions retain their own validity rules. Byte
dictionary sizes, outdegrees, canonical ranks and inactive sentinels stay wide;
slot 255 is a usable code, and a regular cut can lie after all 256 symbols.

Symmetric AD uses the same reachability engine inside recursive validity checks.
Its rank map is rebuilt whenever the host grows, and its adjacency belongs to
the current recursive domain. It replays the original inspection order to retain
early failures and public counters. Ordered trace events use scalar evaluation.
Recursive checks use the fused worklist by default: a domain is checked once,
so its packed layout cannot be reused across lifting snapshots. The SIMD
variants remain selectable. Public attractor counts retain the number of
logical lazy-position tests; physical work is reported separately by the
backend diagnostics.

The PM packed backend is opt-in. One lane represents a component of one measure,
rather than a vertex or an independent game. Eligible Strahler runs have
`2 <= k <= 9`, `0 <= t <= 7`, and a depth below 32767. Other parameters, succinct
measures and unencodable initialized labels use native scalar. Packed state stays
in the lifting loop; native labels are materialized at the result boundary. A
compact symbol cache retains the native successor's carry, refill, zero-budget
and TOP-payload behavior. The verifier remains independent.

| `STRAHLER_PM_BACKEND` | Implementation |
| --- | --- |
| unset, `auto`, or `scalar` | Native scalar, the default |
| `packed` | Packed reference |
| `lookup` | Packed with nonleading-bit lookup (SSSE3) or NEON counts |
| `compare` | Packed with first-discrepancy comparison |
| `both` | Both packed optimizations |

Packed PM and byte AD remain opt-in because their end-to-end costs can exceed
their savings. `STRAHLER_BACKEND_REPORT=1` reports actual dispatch, sweeps, sparse
completion, dictionary capacity fallbacks, edge padding, and layout/dictionary
CPU time on stderr. Collect these diagnostics separately from timed runs.

`src/ad_batch.h` provides an opt-in native fixed-host backend for one to eight
independent requests sharing a residual graph. Eight 16-bit lanes share edge
walks and a vertex queue with pending lane masks. Larger hosts or degrees use
independent native calls. Conditional requests replace cutoff and higher
vertices by the specified absorbing loops. Their results certify the modified
arenas only; this backend is not used to shave the original game or change the
notebook's adaptive schedule. The original-game rejection regression is in
`tests/test_ad_batch.c`.

## Reproduce

```sh
meson setup build-simd-check --buildtype=debug -Dlenstree=disabled -Dwerror=true
meson compile -C build-simd-check -j2
meson test -C build-simd-check test_ad_reach test_ad_reach_alloc test_ad_batch test_pm_packed test_lift_backends --print-errorlogs
STRAHLER_AD_BACKEND=scalar STRAHLER_PM_BACKEND=scalar meson test -C build-simd-check test_ad_lift test_ad_sym test_pm_lift --print-errorlogs
python3 tests/test_lift_backends.py build-simd-check --cases 32
meson setup build-simd --buildtype=release -Db_ndebug=true -Dlenstree=disabled
meson compile -C build-simd -j2
python3 scripts/benchmark_lifting.py build-simd --samples 3 --output /tmp/lifting-timings.json
bzip2 -dc 'jupyter-examples/nester/Nestern=3_compact.gm.bz2' > /tmp/nester3.gm
build-simd/test_ad_batch /tmp/nester3.gm
build-simd/test_ad_reach --benchmark
```

The batch driver first saturates unary iterative AD, chooses the two highest
remaining priorities per parity, and compares one, two, four and eight real
conditional requests on unary and binary hosts. Every final label, round count,
lift count and certified region must match independent native runs.

The handoff baseline is PR #40 at
`3d63d4ffb1fe49e916f162e490516b8e7951a569`. The earlier archive timings used a
different shaving policy and are not substituted for fresh native measurements.
The PM implementation retains its Apache-2.0 notices. The original SIMD PM
artifact is pinned at
[`85230b6d`](https://github.com/vfluegel/oink/tree/85230b6d06307adbf6db6e474ec5fcf546b09400).

## Measurements

Measured on an Intel Core i3-8100T (four CPUs), Linux x86-64, GCC 15.3.1,
`-O3`, with release assertions disabled and no native-CPU compiler flags.
Each CLI arm has one warm-up and three interleaved samples. Verification stays
enabled. Complete JSON and statistics agree within every comparison.
[CLI results](experimental/lifting-simd.csv) include median wall/CPU times,
wall-time spread, peak RSS and untimed layout/dictionary diagnostics.
[Build provenance](experimental/lifting-simd-metadata.json) records the binary,
source, handoff and archive checksums.

| Input / method | Original scalar | Selected backend | Speedup |
| --- | ---: | ---: | ---: |
| Compact Nester 3, AD | 0.453 s | 0.137 s, SIMD | 3.30× |
| Compact Nester 4, AD | 4.577 s | 1.221 s, SIMD | 3.75× |
| Plain Nester 3, AD | 2.022 s | 0.860 s, SIMD | 2.35× |
| Compact Nester 4, symmetric AD | 1.664 s | 0.778 s, fused | 2.14× |
| Plain Nester 3, symmetric AD | 0.507 s | 0.233 s, fused | 2.18× |
| FairScheduler 2, AD | 2.577 s | 1.369 s, SIMD | 1.88× |
| FairScheduler 2, symmetric AD | 1.104 s | 0.890 s, fused | 1.24× |

These gains combine the shared worklist with SIMD. For compact Nester 4 AD,
the fused scalar worklist alone takes 1.660 s; SIMD reduces that to 1.221 s,
an additional 1.36×. Each AD comparison uses lexicographic search with iterative
shaving. The PM comparisons use fixed `(3,2)`, or `(2,1)` for FairScheduler 2,
and can return partial results. They measure the packed backend's cost on the
same host rather than a complete adaptive solve.

Packed PM gives mixed results: `both` takes 0.037 s versus native 0.040 s on
plain Nester 3, but 4.996 s versus 4.165 s on FairScheduler 2. Compact Nester 4
is effectively flat (0.111 versus 0.110 s). These results support keeping native
scalar as the default. Tiny CLI cases take about 1 ms and are dominated by
startup. Byte AD takes 3.366 s on compact Nester 4, compared with
1.660 s for fused scalar, so it also remains opt-in.

[Conditional-batch results](experimental/lifting-simd-batch.csv) use the same
native hosts and compare every label and counter. Eight binary-host requests
on compact Nester 4 take 0.789 CPU s versus 1.153 s for independent fused
calls, a 1.46× gain. Four binary requests are approximately tied; short batches
can lose. These are conditional fixed-host phase timings, separate from the
full CLI results above.

[Synthetic snapshot results](experimental/lifting-simd-snapshots.csv) separate
layout construction and closure costs on 8,192-vertex chains and mixed-degree
graphs. The chain needs 8,192 pure dense sweeps. Sparse completion avoids that
cost, although the fused queue remains faster on this fixture. Layout and byte
dictionary construction can outweigh savings when a layout is used only once.
The PM diagnostic ISA tag describes CPU capabilities; its packed kernels use
SSSE3 or NEON as selected above.

The archived binary-host byte replay at `799a2db` reproduces all counts:

| Compact input | Snapshots | Maximum symbols | Predicate checks | Vertex checks |
| --- | ---: | ---: | ---: | ---: |
| Nester 3 | 3,044 | 86 | 3,420,066 | 3,232,728 |
| Nester 4 | 10,952 | 250 | 35,305,696 | 40,095,272 |

Local validation passed 353 tests, 48 expected CLI-rejection cases, 2,496 seeded
document/statistics comparisons, 261,121 independent bitstring pairs and
428,430 native successor comparisons. Address/undefined-behavior/leak sanitizers
and Valgrind passed. The live Oink oracle was unavailable locally; CI retains
its pinned oracle job. Tracked decompositions pass `--check` unchanged.

Notebook 08 was freshly rerun on all six methods with 600 seconds per case,
2 GiB per process and two workers. All 42 rows retain their non-timing fields;
20 cases complete and six time out, leaving 16 beyond their respective
frontiers. No additional Nester size finishes within the budget. AD and
symmetric AD without shaving still stop at n=4; their `+iter` variants still
stop at n=5; both PM variants still stop at n=4. On n=4, AD+iter falls from
28.319 to 13.667 s, and symmetric AD+iter from 9.851 to 6.182 s. The saved
notebook changes only the two result-table outputs; its prose and diagrams
are unchanged.
