# Tests

Meson builds the C unit tests and runs the command-line regression cases.

- `test_stree.c` tests the reusable Strahler-tree generator.
- `test_cli_parse.cpp` checks the shared C++ CLI integer parser, including
  overflow and malformed arguments.
- `test_cli.py` checks each executable's help, version, long options, error
  status and diagnostics, rejection of short options and bad values, and
  leaf selection when counts saturate.
- `test_source_version.py` builds a source copy inside an unrelated tagged
  checkout and checks that its version has no Git suffix.
- `test_gen_decomp.py` checks every tree metric on hand-built, empty and
  progress-measure trees, including node-based leaf depths. It also checks
  deterministic inventory and `.metrics` generation, stale outputs and PNG reuse.
- `test_make_trees.py` checks batch generation, final-round metrics, agreement
  between `.metrics` files and CSV columns, solver errors and optional Graphviz.
- `test-wrappers.sh` checks wrapper forwarding and propagation of memory-check
  failures through the golden harness.
- `test_ordered_tree.c` tests the owned compact-leaf tree, including stable
  sibling order, normalization, round trips, invalid input, DOT, and p-level
  partitions.
- `test_pg_game.c` checks streaming PGSolver parsing and every `games/*.pg`
  fixture with chunks of 1, 2, 3, 7, 64 and 65,536 bytes, comparing all vertex,
  name, start and CSR fields (or diagnostics for invalid fixtures). It also
  checks long names, integer limits, replacement declarations, I/O errors,
  canonical round trips, and malformed input including stray timeout output.
- `test_pg_game_alloc.c` fails every successive allocation in a separate test
  build of the reader, checking clean failure for dense, sparse, reordered,
  duplicate and named inputs. This C test runs in the ASan and Valgrind jobs.
- `test_pg_game_property.py` compares 120 seeded games against an independent
  reference normalizer, varying whitespace, names, IDs, duplicates and starts.
- `test_pg_game_memory.py` generates a 46 MB game in the build directory and
  parses it under a 240 MiB address-space limit. It exits 77 on macOS, sanitizer
  builds, memory-check wrappers or platforms where that limit is unavailable
  or ineffective.
- `test_pg_set.c` tests the owned dense-index bitset and its move semantics.
- `test_pg_preprocess.c` checks shared option parsing, each shaving rule,
  fixpoint iteration, full-game closure verification, compression bounds, and
  lifting/Zielonka results on the remaining game (after preprocessing and
  earlier shaving rounds) against the exhaustive solver. Separate 30-second
  scaling cases exercise loops, wcwc, and all on 100,000-vertex games
  with independent loops or wcwc candidates without a controlled cycle.
- `test_pg_attractor.c` compares the predecessor-queue attractor against an
  independent fixed-point implementation on fixed and random subgames, and
  does the same for the reach-through-safe attractor with non-total safe sets,
  including unsafe player and opponent exits.
- `test_ad_lift_tree.c` checks the flattened `leafy_∞(T)` host: position order,
  levels, subtree blocks, `after`, `−∞`, `c+`, top, priority compatibility,
  and the skipping next-compatible search against a linear scan on Strahler
  and full `A`-ary hosts.
- `test_ad_lift.c` checks the lifting solver on the sample games, compares
  full-host regions with the exhaustive solver and with Zielonka on random
  games, checks that restricted Strahler hosts give subsets of the winning
  regions, compares the round and single-vertex schedules, exercises the direct
  labelling verifier and materialization failures, and covers adaptive-t,
  full `A`-ary, tree, and host-limit modes. The full `n`-ary host is also
  compared with the exhaustive solver.
  Iterative shaving is checked against Zielonka on 60 seeded random games
  and all disagreement fixtures, including fixed, k-ary, file, adaptive,
  seeded adaptive, and symmetric hosts. Complete results must match both
  winning regions; partial results must certify only correct winners.
  Corrupted round certificates and attractors are rejected. CLI goldens
  cover two-round fixed lifting, both adaptive modes, lifting-sym, no
  progress, exhausted budgets, initial shaving, and renamed flag rejection.
- `test_zielonka.c` checks exact classic and tree-relative decomposition
  witnesses and metrics, verifies literal priority-gap wrappers, and compares
  fixed-seed random games with an exhaustive positional-strategy solver. It
  also rejects non-total domains, invalid bounds and corrupted witnesses.
- `test_ad_tree_dot.c` checks deterministic classic, tree-relative, and
  Jurdziński DOT, priority-level display, label modes, partial results with
  dominions and unresolved vertices, and invalid writer input.
- `run-single-test.sh` runs one command-line golden comparison, accepting
  literal standard input or an `@file` input source.
- `run-regression.sh` regenerates or checks the complete golden suite.
- `run-with-input.sh` supplies a file on standard input for smoke tests.
- `games/` contains parser and solver fixtures.
- `trees/` contains leaf-stream host trees for `pg2adot --tree-file`.
- `golden/` contains expected command output.
- `actual/` receives ignored output from the current test run.

## Compact leaf streams

`pms2dot` and the ordered-tree library read an ordered list of root-to-leaf
paths. `0` and `1` form an edge label, `e` is an explicit empty contribution,
`,` separates components, and `|` terminates a path. The trivial one-node tree
is `|`. Empty normalized edge labels serialize as `e`. For compatibility, a
single trailing comma before `|` is accepted as a level terminator; empty
components anywhere else are rejected.

Distinct children retain the order in which their normalized labels first
occur. Duplicate paths and prefix conflicts are rejected, so leaves and
internal nodes remain distinct.

`pms2dot --check-order` requires the input branches to increase in the
bitstring-vector order from the paper, while `pms2dot --reorder` sorts them
before constructing the tree. For bitstrings this is the infinite binary
tree's depth-first (in-order) relation `0 beta < epsilon < 1 beta`, applied
recursively after common leading bits. Vectors use its lexicographic lifting.

## PGSolver games

The parser accepts an optional `parity MAX_ID;` header, an optional `start ID;`
directive, and one or more node declarations. The start ID is ignored. IDs
need not be contiguous, every final declaration must have a successor, and
every successor must name a final declaration. A later declaration replaces
an earlier declaration with the same ID. Internally, vertices are sorted by
external ID and stored densely with outgoing and predecessor CSR arrays.

`pgfilt` prints graph counts by default and `pgfilt --normalize` emits a
canonical representation suitable for round-trip tests. Priority compaction
is opt-in and preserves the order, parity, and identity of every distinct
priority.

`pg2adot` keeps proof metadata in an attractor-decomposition witness. Its DOT
tree contains only recursive child decompositions: top attractors are node
metadata, while traps and child attractors are edge metadata. Count and set
goldens cover the classic, tree-relative, and Jurdziński views and are parsed
by Graphviz during the Meson suite. In the tree-relative view, each node is
checked as the partition `V = H + T + R_1 + ... + R_k + S`, with every `R_i`
a trap for the other player in the current subgame Gᵢ. The Jurdziński view
uses the same checked partition to expand the witness into `leafy(T)`, with
visible level ranks and empty added leaves omitted. The solver and verifier
use compact priorities; the DOT writer can show the corresponding source
bounds without changing the verified witness.

See [reading attractor decompositions](../docs/developing.md#reading-attractor-decompositions)
for the output labels and complete/partial result conventions.

The goldens come from the lifting solver unless they name Zielonka. On
`ordered_two_children.pg` and the priority-gap games, lifting and Zielonka
produce identical DOT, so `pg2adot_counts.out` also serves as the legacy
Zielonka regression. `lifted_shape.pg` shows a game where the least lifted
decomposition differs from the Zielonka one. `strahler_two.pg` needs a
Strahler-2 decomposition for Even, so `--tree-k=1` gives a partial result.

`disagree_*.pg` and their paired JSON goldens compare the complete ordered
trees, all classic sets, leaf counts, heights, and Strahler numbers of both
algorithms. `test_research.py` checks these witnesses, the host-relative
labelling order, the independent Python lifting model, and an adaptive host
transition where a certified dominion shrinks. See `docs/disagreements.md`
and `docs/incremental-t.md` for the experiments and proofs. Incremental C
tests compare complete label arrays and trees with cold lifting, including
partial results, start values, caps and host budgets.

`test_ad_sym` compares all four solvers against the enumerating reference on
600 deterministic random games (up to 12 vertices and priorities 0 to 6), with
and without shaving/compression. Restricted unary, binary, and uneven file
hosts must agree with ordinary lifting's certified dominions; every classic
and tree-relative decomposition verifies. `thesis_h4/h8.pg` and `thesis_f4/f8.pg`
implement the families in thesis §8.1 (layers numbered from 1 for H and 0 for F).
The unit test asserts exact nonempty call counts for both Zielonka variants:
H₄ 50/23, H₈ 898/83, F₄ 28/24, F₈ 241/112, using literal fixture priorities.
CLI goldens also cover the shared preprocessing, all views, restricted partial
results, statistics, and rejected symmetric option combinations.
The two-vertex `higher_side_entry` regression checks the recursive call with a
higher-player TOP label for both symmetric algorithms. Debug builds also compare
raw labels with independent least labellings before certification on small games;
see [the corrected invariant](../docs/symmetric.md) for the bounds and proof sketch.

## Running the suite

Build and run from the repository root:

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

`ci/check-valgrind.sh build` runs the native tests under Valgrind, excluding
the `python` and `graphviz` suites. Memory errors exit 99, failing even
expected-failure cases. Python and Graphviz tests still run in the regular
test job. The script sets `STRAHLER_TEST_WRAPPER` so golden, stdin, and version
harnesses also wrap their child binaries. Wrapper arguments are whitespace-separated, without shell
evaluation. Valgrind's `--quiet` keeps successful runs' goldens unchanged;
memory errors appear in the Meson error logs.

To regenerate golden files after an intentional output change:

```sh
tests/run-regression.sh --generate-goldens
```

This regenerates the CLI goldens, including both `pg2pmdot` backends and the
symmetric `pg2adot` statistics cases. Build the tools first; the script looks
in `build/`, the project root, and `PATH`. `GENERATE_GOLDENS=1` is equivalent
to `--generate-goldens`. Review `git diff -- tests/golden/` before committing
intentional output changes.

## Progress-measure backends

`test_pm_lift.c` checks both cores against Zielonka on the small fixtures and
200 reproducible generated games, with shaving/compression and full-game /
incremental schedules. Fixed zero-bit budgets must give sound partial
regions. Successor checks cover zero budgets, truncation and strictly
increasing measures through top. Corrupted measures and invalid inputs must
be rejected. `pm_*` goldens cover DOT and leaf output, both players, partial
results, set truncation, preprocessing, original/compact priority labels,
statistics, empty remaining games and per-round incremental output.

`check-pm-oracle.sh` compares the unprefixed Even stream on `pm_cycle.pg`
against `oink_strpm_cycle.out` and `oink_sspm_cycle.out`. The streams `0|00|`
and `e|0|` have also been verified against a live Oink build with the printer
fixes below.

### Live Oink oracle

`scripts/compare_oink.py` uses only Python's standard library. It checks 500
random games per algorithm by default (seed 0), the quick `examples/*.pg`,
and small regression fixtures. Each command has a timeout; only slow examples
may be skipped. Failures save a `.pg` reproducer and transcripts under
`build/oink-failures/` (overridable with `--failures-dir`). Scratch files follow
`TMPDIR`. Meson's optional test checks `OINK_BIN` at runtime and skips with
exit 77 when it is unset:

```sh
OINK_BIN="$TMPDIR/oink-build/oink" meson test -C build oink-oracle --print-errorlogs
# Or run directly, with a different seed/count:
OINK_BIN="$TMPDIR/oink-build/oink" python3 scripts/compare_oink.py build/pg2pmdot --seed=1 --games=500
```

Both solvers receive the same game with IDs assigned in priority/ID order.
This avoids Oink's unspecified ordering of equal-priority vertices, which can
change untruncated Strahler tails even when winning regions agree. Oink keeps
its default parity-preserving priority renumbering. Stream runs disable
single-parity, self-loop and winner-controlled-cycle preprocessing. Ours uses
`--iter-shave --format=leaves`; succinct fixes `--bits=ceil(log2(n))` to match
`--sspm`, bypassing our default bounded schedule. The comparison selects the
last round's last-run player (Oink's counter player is our winner's opponent)
and checks a multiset of the exact printed branches, retaining multiplicities.
It also checks the final parameters and winning regions. With preprocessing
enabled, Oink's solution file is compared with our full `--initial-shave=all` regions.
Oink's `--sol` reads a solution; `--output` writes one.

The `oink-oracle` CI job builds `vfluegel/oink`, branch `print-pms`, pinned to
`51c2640fe650fd18d9331cfe5133cf3605a98c31`, with CMake. Lace is fetched by
Oink's CMakeLists; Boost filesystem, iostreams, random and regex are installed.
Before building, it applies [oink-print-pms.patch](../scripts/oink-print-pms.patch)
to correct two upstream printer defects, without changing active lifts:

- Both backends initialize disabled vertices to bottom and print their unused
  measures. The patch marks those slots top. On
  [oink_disabled.pg](games/oink_disabled.pg), native Strahler prints
  `00,e,e|e,0,e|e,00,e|`; the first branch belongs only to disabled vertices.
  The corrected stream is `e,0,e|e,00,e|`.
- Succinct's final print reads `pm_d` after `delete[]`. The patch moves printing
  before deletion; the comparator takes the last printed run. Even the
  two-vertex [pm_cycle.pg](games/pm_cycle.pg) reaches this use-after-free;
  its native output can happen to look correct depending on the allocator.

The upstream Strahler search can also exhaust its outer `k` bound without
solving the game or printing a stream. The minimal
[oink_search_limit.pg](games/oink_search_limit.pg) is a two-vertex cycle with
priorities 0 and 1, both owned by Odd. Oink stops after `(k,t)=(1,1)` and writes
`paritysol 0;`, although Odd wins both vertices. Our matching incremental mode
has the same partial result; full-game mode solves it at `(2,1)`. The harness
reports these missing streams separately, requires the partial regions to
agree, and still compares full shaved winners. If shaved Oink Strahler also
exhausts, Oink's `--zlk` supplies that full winning partition.

Oink's `uintqueue` is LIFO, and its incoming edges are built in reverse
priority order. The port initially used a FIFO queue and ID-ordered incoming
edges, which changed the final untruncated Strahler measures. The live test
exposed this mismatch, and the port now follows Oink's order.
`test_lifo_order` retains a five-vertex reproducer that fails with FIFO.

Seed 0 validation checked 506 games per algorithm: 500 generated, one quick
example and five fixtures. With the printer patch, there were no stream
or winning-region mismatches: 502 Strahler streams and 506 succinct streams
were compared. Four Strahler cases had matching partial results with no Oink
stream (three generated games plus the search-limit fixture). The large
`cabp_swp.ehoa.pg` example exceeded the timeout for both algorithms. The regular
Meson suite also passed, with the live test skipped when `OINK_BIN` was unset.

Tree JSON tests use `json_check.py` as a dependency-free schema checker and
`test_json.py` to compare all DECOMP backends over examples and game fixtures
against uncapped set-labelled DOT. Each PM round also feeds its measures through
`pms2dot` in both formats; generator cases include every parameter example in
`examples/*.hpp`. They also check prefix-tree order, generated
trees, JSON goldens, option conflicts, partial hosts, player selection and write
failures. Small game runs compare raw tree bytes with trace results; large cases
have separate counting-observer result tests in `slow`, using the production
trace writer for the actual final event. Expensive DOT/JSON pairs have
explicit tests in `slow`, following PR #30 and adding both PM algorithms on
`cabp_swp.ehoa.pg` and `FairSchedulern=2_compact.gm`. `test_json_alloc` injects each allocation
failure into metrics, measure-tree construction and prefix-tree parsing and
checks cleanup and write-error propagation. No new Python dependencies are used.
