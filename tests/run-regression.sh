#!/usr/bin/env bash
# Regression harness for genstree
# Usage:
#   tests/run-regression.sh                # run tests (requires tests/golden/*)
#   tests/run-regression.sh --generate-goldens
#   or: make regtest

# Locate binary (try common names/locations)
BIN=""
if [ -x "./genstree" ]; then
  BIN="./genstree"
elif [ -x "./gstree" ]; then
  BIN="./gstree"
elif command -v genstree >/dev/null 2>&1; then
  BIN="$(command -v genstree)"
elif command -v gstree >/dev/null 2>&1; then
  BIN="$(command -v gstree)"
else
  echo "Error: genstree binary not found in project root or PATH."
  echo "Please build the project so that './genstree' exists, or adjust this script."
  exit 2
fi

echo "Using binary: $BIN"

# Test cases: list of "k t h" triples (adjust/add as desired)
CASES=(
  "2 1 2"
  "3 1 3"
  "4 2 4"
  "3 2 4"
  "1 2 4"
  "1 2 2"
  "2 2 2"
)

mkdir -p tests/actual tests/golden

# Decide whether to generate goldens
GENERATE=0
if [ "${1:-}" = "--generate-goldens" ]; then
  GENERATE=1
fi
if [ "${GENERATE_GOLDENS:-0}" = "1" ]; then
  GENERATE=1
fi

if [ "$GENERATE" -eq 1 ]; then
  echo "Generating golden files into tests/golden/ ..."
  for ct in "${CASES[@]}"; do
    set -- $ct
    k=$1; t=$2; h=$3
    name="k${k}_t${t}_h${h}.out"
    echo "  $BIN -k $k -t $t -h $h > tests/golden/$name"
    "$BIN" -k "$k" -t "$t" -h "$h" > "tests/golden/$name"
  done
  echo "Done. Review and commit tests/golden/* if outputs are correct."
  exit 0
fi

echo "Running regression tests (comparing to tests/golden/)..."
FAIL=0
for ct in "${CASES[@]}"; do
  set -- $ct
  k=$1; t=$2; h=$3
  name="k${k}_t${t}_h${h}.out"
  actual="tests/actual/$name"
  golden="tests/golden/$name"

  echo "  Running: $BIN -k $k -t $t -h $h"
  "$BIN" -k "$k" -t "$t" -h "$h" > "$actual"

  if [ ! -f "$golden" ]; then
    echo "  MISSING GOLDEN: $golden"
    echo "  To create goldens: tests/run-regression.sh --generate-goldens"
    FAIL=1
    continue
  fi

  if ! diff -u "$golden" "$actual"; then
    echo "  FAIL: output differs for $name"
    FAIL=1
  else
    echo "  OK: $name"
  fi
done

if [ "$FAIL" -ne 0 ]; then
  echo "Regression tests failed"
  exit 1
fi

echo "All regression tests passed"
exit 0
