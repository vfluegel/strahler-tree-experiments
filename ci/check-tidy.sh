#!/usr/bin/env bash
set -euo pipefail

if [[ $# -gt 1 ]]; then
  echo "Usage: $0 [BUILD_DIR]" >&2
  exit 2
fi

build_dir=${1:-build}
tidy=${CLANG_TIDY:-clang-tidy-18}

"$tidy" --verify-config
mapfile -d '' sources < <(
  find src tests -type f \
    \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) -print0
)
"$tidy" -p "$build_dir" "${sources[@]}"
