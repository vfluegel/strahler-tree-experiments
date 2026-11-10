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
host caps. `lexicographic-results.json` records the new AD/PM experiment,
with and without iterative shaving, plus the Nester-1 certificate trees.
Each case has a 30-second wall deadline and a 2-GiB address-space limit on Linux.
The recorded library version and binary checksum identify the build used.
Notebook 07 keeps its original n<=4 live experiment.
