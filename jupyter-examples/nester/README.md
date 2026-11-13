# Nester games (CC0)

Original `.gm.bz2` files for plain n=1…7 and `_compact` n=1…4, from the `mlsolver`
archive of Jeroen Keiren (2018), *Parity games*, figshare,
[DOI 10.6084/m9.figshare.6004130.v1](https://doi.org/10.6084/m9.figshare.6004130.v1).
The dataset is dedicated to the public domain under **CC0**.
See [Benchmarks for Parity Games, FSEN 2015](https://arxiv.org/abs/1407.3121)
and the [generator](https://github.com/jkeiren/paritygame-generator).
MLSolver (Friedmann and Lange) generated the validity games for NesterSpec;
`_compact` selects `--option comp`. The archive has n=1…7; requested n=8 is absent.

`history.csv` is the Nester-only extract of the repository's `keiren/results.csv`,
with every original column and row preserved. Its solver_build column records
an older solver. Notebook 07 reads the full repository CSV (also bundled in
the notebooks zip), falling back to this extract with a message if it is absent.
The compressed games are unchanged from the downloaded archive.

Notebook 08 uses all seven plain games. `plain-manifest.json` records their
SHA-256 checksums, graph sizes, normalized priority bounds and theoretical
host caps. `lexicographic-results-repeat-shaving-600s.json` contains the current
comparison: every method is remeasured from n=1 with a 600-second / 2-GiB budget,
repeating `(k,t)` after a productive shaving round and advancing only when a
round removes no vertices. `lexicographic-repeat-shaving-600s.json` stores the
fresh measurements and each method's stopping frontier. Every displayed
measurement comes from the corrected build; older results serve only as a
winning-region reference and remain unchanged.

Run `python jupyter-examples/lexicographic_nester.py rerun-timeouts --frontier --rerun-all --seconds 600 --jobs 2`
from the repository root with the corrected checkout's library selected,
followed by `python jupyter-examples/lexicographic_nester.py collect`.

The preserved `lexicographic-results-600s.json` contains the earlier comparison,
including the earlier successes and ten-minute reruns. `lexicographic-timeouts-600s.json`
records those new measurements with the same 2-GiB memory limit. Each method
starts at its first previous timeout and proceeds in increasing n only after
a success. Its first unsuccessful run stops that method, leaving larger
instances unmeasured at 600 seconds. The file records each method's frontier.
Completed cases retain their original times and provenance.

Run `python jupyter-examples/lexicographic_nester.py rerun-timeouts --frontier --seconds 600 --jobs 2`
from the repository root with the checkout's library selected, followed by
`python jupyter-examples/lexicographic_nester.py collect`. Checkpoints resume
only when the binary, fixtures, baseline and limits match.

The preserved `lexicographic-results-300s.json` contains one result per game/method
for AD, symmetric AD and PM, with and without iterative shaving, under a
five-minute budget and a 2-GiB address-space limit on Linux. It includes the Nester-1 certificate
trees. Each row identifies its source recording; binary and fixture checksums
preserve the measurement provenance. Completed cases retain their measured
time; unresolved cases through Nester-5 were rerun with the full five-minute
allowance. After all four methods timed out there, the run stopped. Nester-6–7
are marked beyond the frontier, with no five-minute measurement reported.

`lexicographic-symmetric-300s.json` adds `AD-sym` and `AD-sym+iter` under the
same limits and stopping point, using `lifting-sym` with
`search='lexicographic'`. The original four methods' measurements are preserved.
From the repository root, with the checkout's library selected, run
`python jupyter-examples/lexicographic_nester.py run-symmetric --jobs 2` and
then `python jupyter-examples/lexicographic_nester.py collect` to reproduce
the extension. The first command resumes completed cases only when the binary,
fixtures and limits match.

The original recordings are `lexicographic-results.json` and
`lexicographic-timeouts-{60,300}s.json`. The first run used one worker;
the five-minute rerun used two, each with its own memory and time limit.
`python lexicographic_nester.py collect` publishes the newest completed comparison,
preserving measurement provenance and each method's stopping point.

Notebook 07 keeps its original n<=4 live experiment.
