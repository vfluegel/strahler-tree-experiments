# Recursive ladder games

Notebook 11 uses PGSolver's `recursiveladder(n)` family, collected in
[Keiren's CC0 dataset](https://doi.org/10.6084/m9.figshare.6004130.v1).
The n=100 and n=200 files are unchanged members of archive `pgsolver`, Figshare
file 10792910. The other values, 2, 4, 8, 16, 32, 64 and 300, use the same
construction. Each game has 5n vertices and 11n-3 edges; priorities and host
heights grow with n.

`recursive_ladder` in `scripts/gen_notebook_families.py` ports the
[PGSolver generator](https://github.com/tcsprojects/pgsolver/blob/d3cd624b61cd66739c45aff9554f6bbdcdd05401/src/lib/generators/recursiveladder.ml).
Its graphs match both archived cases, including priorities, owners and edge
sets. Edge printing order can differ. PGSolver is by Oliver Friedmann and
Martin Lange; the port retains its [BSD license](PGSOLVER-LICENSE.md).
The manifest pins fixture and source checksums.

From a checkout, check bundled fixtures offline, or omit `--check` to regenerate
missing inputs:

```sh
python scripts/gen_notebook_families.py --family recursiveladder --check
```

Collect or validate the 60-second, 2-GiB measurements with:

```sh
python jupyter-examples/family_benchmarks.py run --family recursiveladder --build-dir build --seconds 60 --jobs 2
python jupyter-examples/family_benchmarks.py check --family recursiveladder
```

The six methods use no optional preprocessing or same-parity compression.
Fixed maps cover n=32 and n=100, all admissible k and t=0…6. Larger pairs
above a measured solve are skipped for finding Pareto minima; their outcomes
are unknown. This does not assume host inclusion across k. The notebook links
the proof of a sufficient embedding condition.

Symmetric Zielonka provides independent reference winners, within its own
60-second budget. Its n=200 and n=300 runs time out; every returned solver
certificate is still verified natively. The other seven reference runs finish,
and all returned results on those inputs are checked against their winner
bitsets. Times include decompression, native solving and verification, JSON
output and reduction. Full measurements run separately from notebook execution.
