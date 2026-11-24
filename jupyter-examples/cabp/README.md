# CABP_SWP inputs

These are two parameter values of Keiren's protocol equivalence-checking family:
data size 2 and 4, with capacity and window size both fixed at 1. Branching
bisimulation and strong bisimulation are separate series. The branching
simulation selection is byte-identical to branching bisimulation, so we do not
count it as another instance.

The data-size-2 branching game is the existing `examples/cabp_swp.ehoa.pg`.
A symlink reuses it in a checkout; the notebooks release zip includes its
contents. Converting the pinned SYNTCOMP eHOA file with `knor --print-game`
reproduces it byte for byte.

The [SYNTCOMP v2026 repository corpus](https://github.com/SYNTCOMP/benchmarks/tree/4105caf1f1e5fd3b76657879bfce8021d130cbde/parity/keiren)
also supplies both strong-bisimulation inputs as related comparison cases.
Only the branching data-size-2 anchor appears in the original 484-game release
selection, as confirmed by the pinned
[release receipt](https://github.com/SYNTCOMP/benchmarks/blob/f3374c9cab0d829498b4c055ed00ac2375e88e67/selection/releases/syntcomp26v2/parity-v2026.csv). Its
[readme](https://github.com/SYNTCOMP/benchmarks/blob/4105caf1f1e5fd3b76657879bfce8021d130cbde/parity/keiren/readme.md)
explains the 50 MB selection cutoff. The larger branching game was recovered
from [Keiren's CC0 archive](https://doi.org/10.6084/m9.figshare.6004130.v1)
and passed through the same pinned `pg2hoa.py` and Knor conversion. Its eHOA
file is about 130 MB, beyond that selection cutoff. It is a reconstruction,
rather than a file shipped in SYNTCOMP 2026.

`manifest.json` pins source members, conversion tools, input/output checksums,
graph sizes and host bounds. The measured games retain Knor's split-vertex
representation throughout; the original Keiren graphs have different sizes.
Priorities `{0,3,4}` normalize to `{0,1,2}`. This gives AD heights 2/2 and
STRPM heights 2/3, counting the root.

Check the bundled files without a network connection:

```sh
python scripts/gen_notebook_families.py --family cabp --check
```

To recover missing files, omit `--check` and supply `--knor /path/to/knor`.
The script fetches pinned sources and rejects different converted output.

Recorded experiments use **60 seconds and 2 GiB per process**, at most two
solvers together, and no additional AD leaf limit. Independent Zielonka
references have their own identical limits. Times cover input decompression,
native parsing/solving/verification, JSON writing and streaming reduction,
and winner checks. `native_cli_seconds` isolates the native command, including
its JSON output; it is still not a kernel-only timing. The reference time is
separate. `stage` identifies where an observed deadline occurred.

Every returned certificate is checked by the solver. Where the reference
finished, compact winner bitsets also check partial containment and complete
equality on the original game. A reference limit is recorded independently.
Timeouts leave host sufficiency unknown. A series frontier stops only further
adaptive measurements of that method/order/relation; it makes no claim about
the solvability of larger instances. Fixed maps cover the two data-size-2
anchors, every admissible k, and t=0…3. Unmeasured dominated cells are skipped
only because they cannot be new coordinatewise Pareto minima.

Two separately labelled plain-PM follow-ups repeat the fastest successful
adaptive pairs after the declared fixed grid: branching `(3,7)` and strong
`(3,5)`. Both solve; unmeasured predecessors mean these are sufficient pairs,
not confirmed minima. The collector reproduces this follow-up policy.

The adaptive CABP pass overlapped the last Nester jobs on the shared four-CPU
host. Fixed maps, follow-ups and the ladder pass followed afterwards. The
recording preserves that workload note and the measured worker/executable hashes.
New runs store references atomically by content hash, so a shorter-budget
recording cannot overwrite or delete a published reference. A separate 2 GiB
file-size cap applies to temporary reports; reaching it is an output limit.
