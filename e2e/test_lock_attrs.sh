#!/usr/bin/env bash
# The rpm_lock attribute forcearch reaches dnf_lock: the lockfile resolves
# x86_64 packages regardless of the host's architecture.
set -euo pipefail

python3 -c '
import json
import sys

def nevras(path):
    with open(path) as f:
        return sorted(p["nevra"] for p in json.load(f)["packages"])

def check(path, expected, what):
    got = nevras(path)
    assert got == expected, f"{what}: expected {expected}, got {got}"
    print(f"ok: {what}")

check(sys.argv[1], ["expat-2.7.3-1.el10_2.7.x86_64"], "forcearch solves for x86_64")
' "$@"
