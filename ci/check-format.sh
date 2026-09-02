#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 --check|--fix" >&2
}

if [[ $# -ne 1 ]]; then
  usage
  exit 2
fi

formatter=${CLANG_FORMAT:-clang-format-18}
mapfile -d '' sources < <(
  find src tests experimental -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.cc' -o -name '*.cpp' \
    -o -name '*.hpp' \) -print0
)

case $1 in
  --check)
    "$formatter" --dry-run --Werror "${sources[@]}"
    ;;
  --fix)
    "$formatter" -i "${sources[@]}"
    ;;
  *)
    usage
    exit 2
    ;;
esac
