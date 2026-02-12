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
  # Use a temporary file and avoid sed -i for portability between GNU and BSD sed.
  # We use a pattern that matches any non-whitespace characters followed by the binary name,
  # to replace paths with just the binary name. This handles ./bin, /path/to/bin, etc.
  sed -e "s|[^[:space:]]*/genstree|genstree|g" \
      -e "s|[^[:space:]]*/pms2dot|pms2dot|g" \
      -e "s|[^[:space:]]*/lenstree|lenstree|g" \
      "$file" > "$file.tmp" && mv "$file.tmp" "$file"
}

# Helper to compare actual and golden files robustly
compare_files() {
  local golden=$1
  local actual=$2
  local name=$3

  if [ ! -f "$golden" ]; then
    echo "  MISSING GOLDEN: $golden"
    echo "  To create goldens: tests/run-regression.sh --generate-goldens"
    return 1
  fi

  # Sanitize a copy of the golden file to make comparison robust to path differences
  # in existing golden files (e.g. if they were generated on Mac)
  local golden_sanitized="$actual.golden_sanitized"
  cp "$golden" "$golden_sanitized"
  sanitize_output "$golden_sanitized"

  if ! diff -u "$golden_sanitized" "$actual"; then
    echo "  FAIL: output differs for $name"
    rm "$golden_sanitized"
    return 1
  fi
  rm "$golden_sanitized"
  return 0
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

    # Test -l for 1st and last leaf
    total=$("$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" -j | grep -o '[0-9]\+ leaves' | cut -d' ' -f1)
    for l in 1 $total; do
      echo "  $GENSTREE_BIN -k $k -t $t -h $h -l $l > tests/golden/l${l}_$name"
      "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" -l "$l" > "tests/golden/l${l}_$name" 2>&1
      sanitize_output "tests/golden/l${l}_$name"
    done
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
  "$GENSTREE_BIN" -k 2 -t 1 -h 2 -l 0 > /dev/null 2> tests/golden/gen_err_l.out || true
  "$PMS2DOT_BIN" -x > /dev/null 2> tests/golden/pms_err_flag.out || true
  "$LENSTREE_BIN" -k 1 > /dev/null 2> tests/golden/len_err_args.out || true
  "$LENSTREE_BIN" -k 0 -t 1 -h 2 > /dev/null 2> tests/golden/len_err_k.out || true

  for err in gen_err_k.out gen_err_k_val.out gen_err_l.out pms_err_flag.out len_err_args.out len_err_k.out; do
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
  total=$("$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" -j | grep -o '[0-9]\+ leaves' | cut -d' ' -f1)
  for flags in "" "-j" "-d" "-p 1" "-l 1" "-l $total"; do
    prefix=""
    if [ "$flags" == "-j" ]; then prefix="j_"; fi
    if [ "$flags" == "-d" ]; then prefix="d_"; fi
    if [ "$flags" == "-p 1" ]; then prefix="p1_"; fi
    if [[ "$flags" == -l* ]]; then
        l_val=$(echo "$flags" | cut -d' ' -f2)
        prefix="l${l_val}_"
    fi

    name="${prefix}k${k}_t${t}_h${h}.out"
    actual="tests/actual/$name"
    golden="tests/golden/$name"

    echo "  Running: $GENSTREE_BIN -k $k -t $t -h $h $flags"
    "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" $flags > "$actual" 2>&1
    sanitize_output "$actual"

    if compare_files "$golden" "$actual" "$name"; then
      echo "  OK: $name"
    else
      FAIL=1
    fi
  done
done

echo "Testing pms2dot -h..."
name="pms_h.out"
actual="tests/actual/$name"
golden="tests/golden/$name"
"$PMS2DOT_BIN" -h 2> "$actual" || true
sanitize_output "$actual"
if compare_files "$golden" "$actual" "$name"; then
  echo "  OK: $name"
else
  FAIL=1
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

  if compare_files "$golden" "$actual" "$name"; then
    echo "  OK: $name"
  else
    FAIL=1
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

  if compare_files "$golden" "$actual" "$name"; then
    echo "  OK: $name"
  else
    FAIL=1
  fi
done

echo "Testing error cases..."
# genstree missing k
"$GENSTREE_BIN" -t 1 -h 2 > /dev/null 2> tests/actual/gen_err_k.out || true
# genstree invalid k
"$GENSTREE_BIN" -k 0 -t 1 -h 2 > /dev/null 2> tests/actual/gen_err_k_val.out || true
# genstree invalid l
"$GENSTREE_BIN" -k 2 -t 1 -h 2 -l 0 > /dev/null 2> tests/actual/gen_err_l.out || true
# pms2dot invalid flag
"$PMS2DOT_BIN" -x > /dev/null 2> tests/actual/pms_err_flag.out || true
# lenstree missing args
"$LENSTREE_BIN" -k 1 > /dev/null 2> tests/actual/len_err_args.out || true
# lenstree invalid k
"$LENSTREE_BIN" -k 0 -t 1 -h 2 > /dev/null 2> tests/actual/len_err_k.out || true

for err in gen_err_k.out gen_err_k_val.out gen_err_l.out pms_err_flag.out len_err_args.out len_err_k.out; do
  actual="tests/actual/$err"
  golden="tests/golden/$err"
  sanitize_output "$actual"
  if [ ! -f "$golden" ]; then
    if [ "$GENERATE" -eq 1 ]; then
      cp "$actual" "$golden"
      echo "  CREATED GOLDEN: $golden"
    else
      echo "  MISSING GOLDEN: $golden"
      FAIL=1
    fi
  elif compare_files "$golden" "$actual" "$err"; then
    echo "  OK: $err"
  else
    FAIL=1
  fi
done

if [ "$FAIL" -ne 0 ]; then
  echo "Regression tests failed"
  exit 1
fi

echo "All regression tests passed"
exit 0
