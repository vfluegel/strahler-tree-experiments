#!/usr/bin/env bash
set -euo pipefail

# The runner's mirror selector can keep retrying a broken Azure DNS entry.
# Use Ubuntu's official HTTPS endpoints, preserving suites and signing keys.
if [[ $# -eq 0 ]]; then
  set -- /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources
fi
for sources in "$@"; do
  [[ -f "$sources" ]] || continue
  sed -i \
    -e 's|mirror+file:/etc/apt/apt-mirrors\.txt/*|https://archive.ubuntu.com/ubuntu/|g' \
    -e 's|mirror+file:/etc/apt/apt-security-mirrors\.txt/*|https://security.ubuntu.com/ubuntu/|g' \
    -e 's|https\?://azure\.archive\.ubuntu\.com/ubuntu/*|https://archive.ubuntu.com/ubuntu/|g' \
    -- "$sources"
done
