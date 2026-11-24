# Ordinary ladder games

These are the original PGSolver `laddergame(n)` files collected in
[Keiren's CC0 dataset](https://doi.org/10.6084/m9.figshare.6004130.v1), archive
`pgsolver`, file 10792910. The PGSolver family is attributed to Friedmann in
[Keiren's benchmark survey](https://www.jeroenkeiren.nl/assets/publications/Kei2015.pdf).
The parameter grows through all nine archived values, from 100 to 50,000.
Each game has 2n vertices, 4n edges, and priorities 0 and 1.

`manifest.json` pins the archive members, checksums and bounds. AD heights are
2/1 and STRPM heights are 2/2. The experiment keeps preprocessing and optional
same-parity compression off.

```sh
python scripts/gen_notebook_families.py --family ladder --check
```

Omit `--check` to recover missing inputs by selecting archive members over HTTP
ranges, without downloading the whole archive.

The first pass uses **60 seconds and 2 GiB per process**, at most two solvers
together. Independent Zielonka references have their own identical limits.
Times include input, native solving/verification, JSON output and streaming
winner validation, as described in [the CABP notes](../cabp/README.md).
All nine sizes enter the adaptive scaling tables. Fixed maps use the published
endpoints, n=100 and n=50,000, with k=1…2 and t=0…6.

If a fresh fixed run solves at `(1,0)`, that pair is already the unique
coordinatewise minimum. Other cells stay labelled unmeasured: a successful
smaller pair does not prove that changing k preserves a certificate.
