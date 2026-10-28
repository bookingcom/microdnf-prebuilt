#!/usr/bin/env bash
# Args: <test_lock_layers.py> <dnf5> <dnf.conf of a dnf_config>
set -euo pipefail
exec python3 -I "$1" "$2" "$(dirname "$3")/preamble.args"
