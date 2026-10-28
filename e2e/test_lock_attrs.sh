#!/usr/bin/env bash
# rpm_lock attributes excludepkgs, module_enable, module_platform_id, nobest, install_weak_deps and forcearch
# reach dnf5 lock: each lockfile resolves differently from the defaults.
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

check(sys.argv[1], ["ca-certificates-2025.2.80_v9.0.305-1.el9.noarch", "certuser-1.0-1.noarch"],
      "excludepkgs hides the newer ca-certificates")
check(sys.argv[2], ["bestapp-1.0-1.noarch"], "nobest falls back to bestapp-1.0")
check(sys.argv[3], ["mysql-8.4.7-1.module+el9.7.0+90001+e2e00084.x86_64"],
      "module_enable and module_platform_id expose the mysql:8.4 stream")
check(sys.argv[4], ["expat-2.7.3-1.el10_2.7.x86_64"], "forcearch solves for x86_64")
# dnf: install_weak_deps=False in dnf.conf skips Recommends (weakapp Recommends weakdep).
check(sys.argv[5], ["weakapp-1.0-1.noarch"], "install_weak_deps = False leaves the Recommends out")
' "$@"
