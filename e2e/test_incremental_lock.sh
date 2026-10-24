#!/usr/bin/env bash
set -euo pipefail

LOCK_ALREADY_INSTALLED="$1"
LOCK_INCREMENTAL="$2"
LOCK_BOTH="$3"
LOCK_CHANGED_INTEGRITY="$4"

echo "=== Verifying Incremental Lockfile Scenarios ==="

python3 -c '
import json
import sys

# Test 1: Re-requesting already installed package with same integrity
with open(sys.argv[1]) as f:
    d1 = json.load(f)
req1 = d1.get("requested", [])
pkgs1 = d1.get("packages", [])
assert req1 == ["tour"], f"Expected requested=[tour], got {req1}"
assert len(pkgs1) == 0, f"Expected 0 packages, got {len(pkgs1)}"
print("Scenario 1 passed: Already installed package is in requested but not in packages.")

# Test 2: Incremental request of a new package
with open(sys.argv[2]) as f:
    d2 = json.load(f)
req2 = d2.get("requested", [])
pkgs2 = d2.get("packages", [])
assert req2 == ["mystery-devel"], f"Expected requested=[mystery-devel], got {req2}"
pkg_names_2 = [p["name"] for p in pkgs2]
assert "mystery-devel" in pkg_names_2, "mystery-devel should be in packages"
assert "tour" not in pkg_names_2, "tour should NOT be in packages"
print("Scenario 2 passed: Incremental new package resolved without reinstalling previous layer packages.")

# Test 3: Requesting both already-installed and new package
with open(sys.argv[3]) as f:
    d3 = json.load(f)
req3 = sorted(d3.get("requested", []))
pkgs3 = d3.get("packages", [])
assert req3 == ["mystery-devel", "tour"], f"Expected requested=[mystery-devel, tour], got {req3}"
pkg_names_3 = [p["name"] for p in pkgs3]
assert "mystery-devel" in pkg_names_3, "mystery-devel should be in packages"
assert "tour" not in pkg_names_3, "tour should NOT be in packages"
print("Scenario 3 passed: Requested both; only incremental uninstalled package present in packages.")

# Test 4: Package with changed integrity
with open(sys.argv[4]) as f:
    d4 = json.load(f)
req4 = d4.get("requested", [])
pkgs4 = d4.get("packages", [])
assert req4 == ["tour"], f"Expected requested=[tour], got {req4}"
pkg_names_4 = [p["name"] for p in pkgs4]
assert "tour" in pkg_names_4, "tour SHOULD be in packages because its integrity changed"
print("Scenario 4 passed: Package with changed integrity is re-included in packages.")

print("ALL INCREMENTAL LOCK TESTS PASSED!")
' "${LOCK_ALREADY_INSTALLED}" "${LOCK_INCREMENTAL}" "${LOCK_BOTH}" "${LOCK_CHANGED_INTEGRITY}"
