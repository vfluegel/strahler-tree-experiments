# Regression tests for genstree

This directory contains a small regression harness for the `genstree` binary.

- `run-regression.sh` — Run the regression suite or generate golden outputs.
- `golden/` — Expected outputs (golden files).
- `actual/` — Test run outputs (ignored by git).

Usage:
- Build the project (top-level): `make`
- Generate golden files (one-time or when outputs intentionally change):
  - `make regtest GENERATE_GOLDENS=1`
  - or: `tests/run-regression.sh --generate-goldens`
- Run regression tests (compare actual vs golden):
  - `make regtest`
  - or: `tests/run-regression.sh`

If any diffs are reported, the script exits non-zero. To update goldens, run with `GENERATE_GOLDENS=1` (or `--generate-goldens`) and commit the resulting `tests/golden/*` files if the changes are intentional.
