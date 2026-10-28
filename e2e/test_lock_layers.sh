#!/usr/bin/env bash
# Args: <test_lock_layers.py> <dnf_lock>
#   or: <test_lock_layers.py> <dnf5> dnf5 <dnf.conf of a dnf_config>
set -euo pipefail
if [[ "${3:-}" == "dnf5" ]]; then
  exec python3 -I "$1" "$2" dnf5 "$(dirname "$4")/preamble.args"
fi
exec python3 -I "$1" "$2"
