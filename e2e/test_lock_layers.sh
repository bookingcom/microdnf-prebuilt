#!/usr/bin/env bash
# Args: <test_lock_layers.py> <dnf_lock>
set -euo pipefail
exec python3 -I "$1" "$2"
