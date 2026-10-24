#!/usr/bin/env bash
# Runs dnf_lock in the target configuration, where libdnf's assertions are compiled in as in the release
# binaries (rpm_lock actions use the exec configuration, which defines NDEBUG).
# Args: <dnf_lock> <test.repo> <repomd.xml> <lockfile of the layer below>
set -euo pipefail

DNF_LOCK="$1"
REPO_CONF="$2"
REPOMD="$3"
BASE_LOCK="$4"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT
mkdir -p "${TMP}/repos.d"
sed "s|@REPO_DIR@|$(cd "$(dirname "${REPOMD}")/.." && pwd)|" "${REPO_CONF}" > "${TMP}/repos.d/test.repo"

if ! "${DNF_LOCK}" --reposdir="${TMP}/repos.d" --setopt=cachedir="${TMP}/cache" --releasever=9 \
    --archlist=x86_64,noarch --lockfile="${BASE_LOCK}" -o "${TMP}/out.json" mystery-devel 2> "${TMP}/err"; then
  echo "FAIL dnf_lock --lockfile exited non-zero:"
  cat "${TMP}/err"
  exit 1
fi

python3 - "${TMP}/out.json" <<'EOF'
import json
import sys

names = [p["name"] for p in json.load(open(sys.argv[1]))["packages"]]
assert "mystery-devel" in names, names
assert "tour" not in names, names
EOF
echo "ok   dnf_lock resolves on top of a lockfile with assertions enabled"
