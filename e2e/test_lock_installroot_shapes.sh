#!/usr/bin/env bash
set -euo pipefail

echo "Verifying rpm_lock installroot with differently shaped inputs..."
for lockfile in "$@"; do
  echo "  ${lockfile}"
  python3 - "${lockfile}" <<'PY'
import json
import sys

lock = json.load(open(sys.argv[1]))
got = sorted(p["nevra"] for p in lock["packages"])
expected = ["app-1.0-1.noarch", "libfoo-2.0-1.noarch"]
if got != expected:
    sys.exit(f"{sys.argv[1]}: expected {expected}, got {got}")
libfoo = next(p for p in lock["packages"] if p["name"] == "libfoo")
if libfoo.get("replaces") != ["libfoo-1.0-1.noarch"]:
    sys.exit(f"{sys.argv[1]}: libfoo should replace libfoo-1.0-1: {libfoo}")
PY
done
echo "rpm_lock installroot shapes verification passed successfully!"
