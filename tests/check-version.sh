#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 BINARY EXPECTED_VERSION" >&2
  exit 2
fi

binary=$1
expected_version=$2
actual=$("$binary" --version)
expected="${binary##*/} ${expected_version}"

if [[ "$actual" != "$expected" ]]; then
  printf 'Expected %q, got %q\n' "$expected" "$actual" >&2
  exit 1
fi
