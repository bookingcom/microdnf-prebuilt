#!/usr/bin/env bash
set -euo pipefail

LOCKFILE="$1"

echo "Verifying rpm_lock with installroot, excludepkgs and module_enable..."
python3 - "${LOCKFILE}" <<'PY'
import json
import sys

lock = json.load(open(sys.argv[1]))
got = sorted(p["nevra"] for p in lock["packages"])
expected = sorted([
    "app-1.0-1.noarch",
    "ca-certificates-2025.2.80_v9.0.305-1.el9.noarch",
    "certuser-1.0-1.noarch",
    "libfoo-2.0-1.noarch",
    "mysql-8.4.7-1.module+el9.7.0+90001+e2e00084.x86_64",
    "weak-rec-1.0-1.noarch",
    "weak-sup-1.0-1.noarch",
    "weakapp-1.0-1.noarch",
])
if got != expected:
    sys.exit(f"unexpected packages:\n  expected {expected}\n  got      {got}")
libfoo = next(p for p in lock["packages"] if p["name"] == "libfoo")
if libfoo.get("replaces") != ["libfoo-1.0-1.noarch"]:
    sys.exit(f"libfoo should replace the installed libfoo-1.0-1: {libfoo}")
if lock["requested"] != ["app", "certuser", "mysql", "oldtool", "weakapp"]:
    sys.exit(f"unexpected requested list: {lock['requested']}")
PY

echo "rpm_lock installroot verification passed successfully!"
