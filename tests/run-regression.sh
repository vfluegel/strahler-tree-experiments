#!/usr/bin/env bash
# Regression harness for genstree and pms2dot
# Usage:
#   tests/run-regression.sh                # run tests (requires tests/golden/*)
#   tests/run-regression.sh --generate-goldens
#   or: make regtest

# Locate binaries
GENSTREE_BIN=""
if [ -x "./genstree" ]; then
  GENSTREE_BIN="./genstree"
elif command -v genstree >/dev/null 2>&1; then
  GENSTREE_BIN="$(command -v genstree)"
else
  echo "Error: genstree binary not found in project root or PATH."
  echo "Please build the project so that './genstree' exists, or adjust this script."
  exit 2
fi

PMS2DOT_BIN=""
if [ -x "./pms2dot" ]; then
  PMS2DOT_BIN="./pms2dot"
elif command -v pms2dot >/dev/null 2>&1; then
  PMS2DOT_BIN="$(command -v pms2dot)"
else
  echo "Error: pms2dot binary not found in project root or PATH."
  echo "Please build the project so that './pms2dot' exists, or adjust this script."
  exit 2
fi

echo "Using genstree binary: $GENSTREE_BIN"
echo "Using pms2dot binary: $PMS2DOT_BIN"

LENSTREE_BIN=""
if [ -x "./lenstree" ]; then
  LENSTREE_BIN="./lenstree"
elif command -v lenstree >/dev/null 2>&1; then
  LENSTREE_BIN="$(command -v lenstree)"
else
  echo "Error: lenstree binary not found in project root or PATH."
  echo "Please build the project so that './lenstree' exists, or adjust this script."
  exit 2
fi
echo "Using lenstree binary: $LENSTREE_BIN"

# genstree test cases: list of "k t h" triples
GENSTREE_CASES=(
  "2 1 2"
  "3 1 3"
  "4 2 4"
  "3 2 4"
  "1 2 4"
  "1 2 2"
  "2 2 2"
)

# pms2dot test cases: list of progress measure strings
PMS2DOT_CASES=(
  "0|1|"
  "0,0|0,1|1,0|1,1|"
  "0,e,1|0,1,1|1,e,0|"
  "0,0,1,e,1|0,1,1,0,0|"
  "0,1|1,0|0,0|" # trigger swap logic
)

mkdir -p tests/actual tests/golden

# Helper to sanitize paths in output files
sanitize_output() {
  local file=$1
  sed -i "s|$GENSTREE_BIN|genstree|g; s|$PMS2DOT_BIN|pms2dot|g; s|$LENSTREE_BIN|lenstree|g" "$file"
  # Also catch cases where the binary might be called via a different path
  sed -i "s|.*/genstree|genstree|g; s|.*/pms2dot|pms2dot|g; s|.*/lenstree|lenstree|g" "$file"
}

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
  for ct in "${GENSTREE_CASES[@]}"; do
    set -- $ct
    k=$1; t=$2; h=$3
    name="k${k}_t${t}_h${h}.out"
    echo "  $GENSTREE_BIN -k $k -t $t -h $h > tests/golden/$name"
    "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" > "tests/golden/$name" 2>&1
    sanitize_output "tests/golden/$name"

    # Test -j (just count)
    echo "  $GENSTREE_BIN -k $k -t $t -h $h -j > tests/golden/j_$name"
    "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" -j > "tests/golden/j_$name" 2>&1
    sanitize_output "tests/golden/j_$name"

    # Test -d (dot format)
    echo "  $GENSTREE_BIN -k $k -t $t -h $h -d > tests/golden/d_$name"
    "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" -d > "tests/golden/d_$name" 2>&1
    sanitize_output "tests/golden/d_$name"

    # Test -p 1 (partitioning)
    echo "  $GENSTREE_BIN -k $k -t $t -h $h -p 1 > tests/golden/p1_$name"
    "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" -p 1 > "tests/golden/p1_$name" 2>&1
    sanitize_output "tests/golden/p1_$name"
  done
  for i in "${!PMS2DOT_CASES[@]}"; do
    case="${PMS2DOT_CASES[$i]}"
    name="pms_${i}.out"
    echo "  echo \"$case\" | $PMS2DOT_BIN > tests/golden/$name"
    echo "$case" | "$PMS2DOT_BIN" > "tests/golden/$name" 2>&1
    sanitize_output "tests/golden/$name"
  done
  # Test pms2dot -h
  echo "  $PMS2DOT_BIN -h 2> tests/golden/pms_h.out"
  "$PMS2DOT_BIN" -h 2> "tests/golden/pms_h.out" || true
  sanitize_output "tests/golden/pms_h.out"

  for ct in "${GENSTREE_CASES[@]}"; do
    set -- $ct
    k=$1; t=$2; h=$3
    name="len_k${k}_t${t}_h${h}.out"
    echo "  $LENSTREE_BIN -k $k -t $t -h $h > tests/golden/$name"
    "$LENSTREE_BIN" -k "$k" -t "$t" -h "$h" > "tests/golden/$name" 2>&1
    sanitize_output "tests/golden/$name"
  done

  echo "Generating error case goldens..."
  "$GENSTREE_BIN" -t 1 -h 2 > /dev/null 2> tests/golden/gen_err_k.out || true
  "$GENSTREE_BIN" -k 0 -t 1 -h 2 > /dev/null 2> tests/golden/gen_err_k_val.out || true
  "$PMS2DOT_BIN" -x > /dev/null 2> tests/golden/pms_err_flag.out || true
  "$LENSTREE_BIN" -k 1 > /dev/null 2> tests/golden/len_err_args.out || true
  "$LENSTREE_BIN" -k 0 -t 1 -h 2 > /dev/null 2> tests/golden/len_err_k.out || true

  for err in gen_err_k.out gen_err_k_val.out pms_err_flag.out len_err_args.out len_err_k.out; do
    sanitize_output "tests/golden/$err"
  done

  echo "Done. Review and commit tests/golden/* if outputs are correct."
  exit 0
fi

echo "Running regression tests (comparing to tests/golden/)..."
FAIL=0

echo "Testing genstree..."
for ct in "${GENSTREE_CASES[@]}"; do
  set -- $ct
  k=$1; t=$2; h=$3
  for flags in "" "-j" "-d" "-p 1"; do
    prefix=""
    if [ "$flags" == "-j" ]; then prefix="j_"; fi
    if [ "$flags" == "-d" ]; then prefix="d_"; fi
    if [ "$flags" == "-p 1" ]; then prefix="p1_"; fi

    name="${prefix}k${k}_t${t}_h${h}.out"
    actual="tests/actual/$name"
    golden="tests/golden/$name"

    echo "  Running: $GENSTREE_BIN -k $k -t $t -h $h $flags"
    "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" $flags > "$actual" 2>&1
    sanitize_output "$actual"

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
done

echo "Testing pms2dot -h..."
name="pms_h.out"
actual="tests/actual/$name"
golden="tests/golden/$name"
"$PMS2DOT_BIN" -h 2> "$actual" || true
sanitize_output "$actual"
if ! diff -u "$golden" "$actual"; then
  echo "  FAIL: output differs for $name"
  FAIL=1
else
  echo "  OK: $name"
fi

echo "Testing lenstree..."
for ct in "${GENSTREE_CASES[@]}"; do
  set -- $ct
  k=$1; t=$2; h=$3
  name="len_k${k}_t${t}_h${h}.out"
  actual="tests/actual/$name"
  golden="tests/golden/$name"

  echo "  Running: $LENSTREE_BIN -k $k -t $t -h $h"
  "$LENSTREE_BIN" -k "$k" -t "$t" -h "$h" > "$actual" 2>&1
  sanitize_output "$actual"

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

echo "Testing pms2dot..."
for i in "${!PMS2DOT_CASES[@]}"; do
  case="${PMS2DOT_CASES[$i]}"
  name="pms_${i}.out"
  actual="tests/actual/$name"
  golden="tests/golden/$name"

  echo "  Running: echo \"$case\" | $PMS2DOT_BIN"
  echo "$case" | "$PMS2DOT_BIN" > "$actual" 2>&1
  sanitize_output "$actual"

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

echo "Testing error cases..."
# genstree missing k
"$GENSTREE_BIN" -t 1 -h 2 > /dev/null 2> tests/actual/gen_err_k.out || true
# genstree invalid k
"$GENSTREE_BIN" -k 0 -t 1 -h 2 > /dev/null 2> tests/actual/gen_err_k_val.out || true
# pms2dot invalid flag
"$PMS2DOT_BIN" -x > /dev/null 2> tests/actual/pms_err_flag.out || true
# lenstree missing args
"$LENSTREE_BIN" -k 1 > /dev/null 2> tests/actual/len_err_args.out || true
# lenstree invalid k
"$LENSTREE_BIN" -k 0 -t 1 -h 2 > /dev/null 2> tests/actual/len_err_k.out || true

for err in gen_err_k.out gen_err_k_val.out pms_err_flag.out len_err_args.out len_err_k.out; do
  sanitize_output "tests/actual/$err"
  if [ ! -f "tests/golden/$err" ]; then
    if [ "$GENERATE" -eq 1 ]; then
      cp "tests/actual/$err" "tests/golden/$err"
    else
      echo "  MISSING GOLDEN: tests/golden/$err"
      FAIL=1
    fi
  elif ! diff -u "tests/golden/$err" "tests/actual/$err"; then
    echo "  FAIL: output differs for $err"
    FAIL=1
  else
    echo "  OK: $err"
  fi
done

if [ "$FAIL" -ne 0 ]; then
  echo "Regression tests failed"
  exit 1
fi

echo "All regression tests passed"
exit 0
