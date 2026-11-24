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

PM supports Strahler and succinct progress measures. Strahler PM selects the
full Oink-style vector kernel automatically on x86-64 (baseline SSE2) and
AArch64 (NEON). Its 56-byte vertex record stores eight bitstrings, occupancy
masks and levels directly; it has no scalar symbol cache. Non-leading-bit
counts, the three-step parallel prefix scan, successor predicates, tail fill,
and level/string comparisons run across the vector lanes. As in Oink, the
carry on the one selected lane and graph/worklist traversal use scalar control.
Conversion to flat labels happens only after lifting, for tree output and the
independent verifier. Succinct PM keeps its scalar implementation.

`STRAHLER_PM_BACKEND=scalar` selects the native reference. Unset, `auto`, and
`simd` select the vector kernel when `2 <= k <= 9`, `t <= 7`, and
`0 < depth < 32767`. The `k=1` case and wider hosts use the native fallback,
as do unsupported architectures and optional allocation failures. SSE2 and
NEON are baseline instructions on the supported wheel architectures, so this
kernel needs no AVX2, SSSE3 or native compiler flags. The older partial packed
backend and its `lookup`/`compare`/`both` switches have been removed.
Notebooks 05 and 07–11, and their PM workers, explicitly select `simd` and
Strahler PM. SSPM remains available with `--algorithm=succinct`.

The [full solver results](experimental/pm-simd-oink/cli.csv),
[fixed-host comparisons](experimental/pm-simd-oink/fixed.csv), and
[kernel measurements](experimental/pm-simd-oink/kernels.csv) retain the evidence.
Full solves use the same canonical inputs and verify their winning partitions;
fixed-host runs require matching labels, regions, lift counts and attempts.
Oink was built both with and without native CPU flags; its full-solver table
uses its default native build. Current C builds use portable release flags.
The scalar C port already uses flat storage and short measures, so vectorizing
eight component lanes can cost more than the scalar work it replaces.

The histories explain the difference in coverage. Oink introduced its SIMD
Strahler solver in `0199b48` (March 11), vectorized successor predicates and
tail filling in `caeba09` (March 27), changed to contiguous per-vertex records
in `a2a092b` (March 31), replaced the scalar prefix sum in `b4138e3` (April 2),
and vectorized level comparison in `a61c3dd` (April 3). The October 19 C port,
`9081ce8`, used the scalar `strpm.cpp` and `sspm.cpp` from `print-pms`; that
snapshot already contained the separate SIMD implementations. November 11's
`d5685b9` added selected count/string kernels while retaining scalar carry,
refill, level traversal and reconstruction of its component cache. It was a
partial port, rather than a snapshot predating Oink's vectorization.

Before the bounded STRPM correction, full solves showed Oink's SIMD Strahler
solver beating its scalar Strahler solver: 10.22 versus 18.12 seconds on
FairScheduler. However, scalar succinct PM took 1.94 seconds in Oink and 2.51 seconds in the original C implementation;
its SIMD counterpart took 12.29 seconds. Succinct PM also completed the
875,281-vertex SYNTCOMP example where all Strahler variants exceeded 20 seconds.
These are workload measurements, not a claim that one algorithm wins on every
possible game.

The scalar succinct C schedule advances its bit budget after each incomplete round,
as Oink's bounded succinct solver does. Compared with retrying productive
budgets, this improved all five measured cases. FairScheduler fell from
2.26 to 1.14 seconds, and the SYNTCOMP example from 10.48 to 7.25 seconds.
The [schedule comparison](experimental/pm-simd-oink/selection.csv) checks the
complete winning partition with verification enabled. Each arm has one warm-up
and three alternating samples on one pinned CPU. The fixed-host and kernel
comparisons use five samples. Oink's extracted kernels additionally agreed
with the native reference on 912,000 successors and 1,824,000 comparisons.
The [succinct snapshot measurements](experimental/pm-simd-oink/retained.csv)
record the earlier SSPM-only snapshot separately, with source and binary hashes in the
corresponding JSON. Oink's scalar succinct solver remains slightly faster on
the large SYNTCOMP example (6.70 versus 7.09 seconds), while that succinct C
implementation wins on the other measured nontrivial cases.

The [corrected-kernel measurements](experimental/pm-simd-oink/strpm-canonical-kernel.csv)
compare scalar STRPM with full SIMD STRPM in the same portable release binary.
Every sample produces identical JSON, including parameters, trees, certificates,
lift counts and attempts, and matches the independently recorded winning partition.
These are complete adaptive solves with iterative shaving and verification,
one warm-up and three alternating timed samples per arm, pinned to CPU 3.

| Input | Scalar STRPM | SIMD STRPM | Scalar/SIMD |
| --- | ---: | ---: | ---: |
| Compact Nester 3 | 0.00997 s | 0.00931 s | 1.07× |
| Compact Nester 4 | 0.11862 s | 0.08793 s | 1.35× |
| Plain Nester 3 | 0.04502 s | 0.03557 s | 1.27× |
| FairScheduler 2 | 0.13158 s | 0.13172 s | 1.00× |

SIMD improves these Nester runs; the FairScheduler arms are effectively tied.
The bounded-lift fix reduces FairScheduler from four shaving rounds to two.
The earlier [full-kernel recording](experimental/pm-simd-oink/strpm-full-kernel.csv)
and Oink timings above used the old nonstrict lift. SSPM has its own bit budget;
its historical measurements remain separate from these STRPM comparisons.

Native differential tests cover successors, nonstrict canonical completions and
comparisons, including negative cutoffs, t=0, unequal string lengths and the
largest encodable hosts. An independent generated-leaf oracle checks least
bounded measures. Backend and fuzz checks compare complete scalar/SIMD solver
documents. The Oink oracle checks STRPM winner soundness and completed winning
partitions; SSPM retains exact stream and budget checks.

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
meson test -C build-simd-check test_ad_reach test_ad_reach_alloc test_ad_batch test_strpm_simd test_lift_backends --print-errorlogs
STRAHLER_AD_BACKEND=scalar meson test -C build-simd-check test_ad_lift test_ad_sym test_pm_lift --print-errorlogs
python3 tests/test_lift_backends.py build-simd-check --cases 32
meson setup build-simd --buildtype=release -Db_ndebug=true -Dlenstree=disabled
meson compile -C build-simd -j2
python3 scripts/benchmark_lifting.py build-simd --samples 3 --output /tmp/lifting-timings.json
bzip2 -dc 'jupyter-examples/nester/Nestern=3_compact.gm.bz2' > /tmp/nester3.gm
STRAHLER_PM_BACKEND=scalar build-simd/pg2pmdot --algorithm=strahler --iter-shave --format=json --stats /tmp/nester3.gm > /tmp/nester3-scalar.json
STRAHLER_PM_BACKEND=simd build-simd/pg2pmdot --algorithm=strahler --iter-shave --format=json --stats /tmp/nester3.gm > /tmp/nester3-simd.json
cmp /tmp/nester3-scalar.json /tmp/nester3-simd.json
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

## Archived November 11 measurements

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

The removed packed PM experiment gave mixed results: `both` takes 0.037 s versus native 0.040 s on
plain Nester 3, but 4.996 s versus 4.165 s on FairScheduler 2. Compact Nester 4
is effectively flat (0.111 versus 0.110 s). These partial-kernel results
predate the full Strahler SIMD port. Tiny CLI cases take about 1 ms and are
dominated by startup. Byte AD takes 3.366 s on compact Nester 4, compared with
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
The archived PM diagnostic ISA tag describes CPU capabilities; those partial
kernels used SSSE3 or NEON.

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
