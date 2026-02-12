#!/usr/bin/env bash
# Regression harness for genstree and pms2dot
# Usage:
#   tests/run-regression.sh                # run tests (via meson)
#   tests/run-regression.sh --generate-goldens

# Decide whether to generate goldens
GENERATE=0
if [ "${1:-}" = "--generate-goldens" ]; then
  GENERATE=1
fi
if [ "${GENERATE_GOLDENS:-0}" = "1" ]; then
  GENERATE=1
fi

if [ "$GENERATE" -eq 0 ]; then
  echo "Running regression tests via meson..."
  BUILD_DIR="build"
  if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory '$BUILD_DIR' not found. Please run 'meson setup build'."
    exit 1
  fi
  meson test -C "$BUILD_DIR"
  exit $?
fi

# Locate binaries for golden generation
GENSTREE_BIN=""
if [ -x "./build/genstree" ]; then
  GENSTREE_BIN="./build/genstree"
elif [ -x "./genstree" ]; then
  GENSTREE_BIN="./genstree"
elif command -v genstree >/dev/null 2>&1; then
  GENSTREE_BIN="$(command -v genstree)"
else
  echo "Error: genstree binary not found in build/ or project root or PATH."
  exit 2
fi

PMS2DOT_BIN=""
if [ -x "./build/pms2dot" ]; then
  PMS2DOT_BIN="./build/pms2dot"
elif [ -x "./pms2dot" ]; then
  PMS2DOT_BIN="./pms2dot"
elif command -v pms2dot >/dev/null 2>&1; then
  PMS2DOT_BIN="$(command -v pms2dot)"
else
  echo "Error: pms2dot binary not found in build/ or project root or PATH."
  exit 2
fi

LENSTREE_BIN=""
if [ -x "./build/lenstree" ]; then
  LENSTREE_BIN="./build/lenstree"
elif [ -x "./lenstree" ]; then
  LENSTREE_BIN="./lenstree"
elif command -v lenstree >/dev/null 2>&1; then
  LENSTREE_BIN="$(command -v lenstree)"
fi

echo "Using genstree binary: $GENSTREE_BIN"
echo "Using pms2dot binary: $PMS2DOT_BIN"
echo "Using lenstree binary: $LENSTREE_BIN"

GENSTREE_CASES=(
  "2 1 2" "3 1 3" "4 2 4" "3 2 4" "1 2 4" "1 2 2" "2 2 2"
)

PMS2DOT_CASES=(
  "0|1|" "0,0|0,1|1,0|1,1|" "0,e,1|0,1,1|1,e,0|" "0,0,1,e,1|0,1,1,0,0|" "0,1|1,0|0,0|"
)

mkdir -p tests/actual tests/golden

sanitize_output() {
  local file=$1
  sed -e "s|[^[:space:]]*/genstree|genstree|g" \
      -e "s|[^[:space:]]*/pms2dot|pms2dot|g" \
      -e "s|[^[:space:]]*/lenstree|lenstree|g" \
      "$file" > "$file.tmp" && mv "$file.tmp" "$file"
}

echo "Generating golden files into tests/golden/ ..."
for ct in "${GENSTREE_CASES[@]}"; do
  set -- $ct
  k=$1; t=$2; h=$3
  name="k${k}_t${t}_h${h}.out"
  echo "  $GENSTREE_BIN -k $k -t $t -h $h > tests/golden/$name"
  "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" > "tests/golden/$name" 2>&1
  sanitize_output "tests/golden/$name"

  echo "  $GENSTREE_BIN -k $k -t $t -h $h -j > tests/golden/j_$name"
  "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" -j > "tests/golden/j_$name" 2>&1
  sanitize_output "tests/golden/j_$name"

  echo "  $GENSTREE_BIN -k $k -t $t -h $h -d > tests/golden/d_$name"
  "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" -d > "tests/golden/d_$name" 2>&1
  sanitize_output "tests/golden/d_$name"

  echo "  $GENSTREE_BIN -k $k -t $t -h $h -p 1 > tests/golden/p1_$name"
  "$GENSTREE_BIN" -k "$k" -t "$t" -h "$h" -p 1 > "tests/golden/p1_$name" 2>&1
  sanitize_output "tests/golden/p1_$name"

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

echo "  $PMS2DOT_BIN -h 2> tests/golden/pms_h.out"
"$PMS2DOT_BIN" -h 2> "tests/golden/pms_h.out" || true
sanitize_output "tests/golden/pms_h.out"

if [ -n "$LENSTREE_BIN" ]; then
  for ct in "${GENSTREE_CASES[@]}"; do
    set -- $ct
    k=$1; t=$2; h=$3
    name="len_k${k}_t${t}_h${h}.out"
    echo "  $LENSTREE_BIN -k $k -t $t -h $h > tests/golden/$name"
    "$LENSTREE_BIN" -k "$k" -t "$t" -h "$h" > "tests/golden/$name" 2>&1
    sanitize_output "tests/golden/$name"
  done
fi

echo "Generating error case goldens..."
"$GENSTREE_BIN" -t 1 -h 2 > /dev/null 2> tests/golden/gen_err_k.out || true
"$GENSTREE_BIN" -k 0 -t 1 -h 2 > /dev/null 2> tests/golden/gen_err_k_val.out || true
"$GENSTREE_BIN" -k 2 -t 1 -h 2 -l 0 > /dev/null 2> tests/golden/gen_err_l.out || true
"$PMS2DOT_BIN" -x > /dev/null 2> tests/golden/pms_err_flag.out || true
if [ -n "$LENSTREE_BIN" ]; then
  "$LENSTREE_BIN" -k 1 > /dev/null 2> tests/golden/len_err_args.out || true
  "$LENSTREE_BIN" -k 0 -t 1 -h 2 > /dev/null 2> tests/golden/len_err_k.out || true
fi

for err in gen_err_k.out gen_err_k_val.out gen_err_l.out pms_err_flag.out len_err_args.out len_err_k.out; do
  if [ -f "tests/golden/$err" ]; then
    sanitize_output "tests/golden/$err"
  fi
done

echo "Done. Review and commit tests/golden/* if outputs are correct."
exit 0
