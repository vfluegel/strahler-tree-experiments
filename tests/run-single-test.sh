#!/usr/bin/env bash
# tests/run-single-test.sh <binary> <golden_file> <actual_file> <stdin_content_or_EMPTY> <args...>

BINARY=$1
GOLDEN=$2
ACTUAL=$3
STDIN_CONTENT=$4
shift 4

sanitize_output() {
  local file=$1
  sed -e "s|[^[:space:]]*/genstree|genstree|g" \
      -e "s|[^[:space:]]*/pms2dot|pms2dot|g" \
      -e "s|[^[:space:]]*/lenstree|lenstree|g" \
      "$file" > "$file.tmp" && mv "$file.tmp" "$file"
}

compare_files() {
  local golden=$1
  local actual=$2

  if [ ! -f "$golden" ]; then
    echo "  MISSING GOLDEN: $golden"
    return 1
  fi

  local golden_sanitized="$actual.golden_sanitized"
  cp "$golden" "$golden_sanitized"
  sanitize_output "$golden_sanitized"

  if ! diff -u "$golden_sanitized" "$actual"; then
    rm "$golden_sanitized"
    return 1
  fi
  rm "$golden_sanitized"
  return 0
}

mkdir -p "$(dirname "$ACTUAL")"

if [ "$STDIN_CONTENT" != "EMPTY" ]; then
    echo -n "$STDIN_CONTENT" | "$BINARY" "$@" > "$ACTUAL" 2>&1
else
    "$BINARY" "$@" > "$ACTUAL" 2>&1
fi

sanitize_output "$ACTUAL"
if compare_files "$GOLDEN" "$ACTUAL"; then
  exit 0
else
  exit 1
fi
