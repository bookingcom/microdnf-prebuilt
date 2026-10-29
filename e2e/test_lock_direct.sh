#!/usr/bin/env bash
# Runs `dnf5 lock` in the target configuration, where libdnf5's assertions are compiled in as in the release
# binaries (rpm_lock actions use the exec configuration, which defines NDEBUG).
# Args: <dnf5> <test.repo> <repomd.xml> <lockfile of the layer below> <dnf.conf of a dnf_config>
set -euo pipefail

DNF5="$1"
REPOMD="$3"
BASE_LOCK="$4"
DNF_CONF="$5"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT
REPO_DIR="$(cd "$(dirname "${REPOMD}")/.." && pwd)"

source "$(dirname "$0")/dnf5_lib.sh"
mkdir -p "${TMP}/root"
dnf5_preamble "${DNF_CONF}" "${REPO_DIR}" "${TMP}"
lock=("${DNF5}" "${PREAMBLE[@]}" lock)

if ! "${lock[@]}" --archlist=x86_64,noarch --lockfile="${BASE_LOCK}" -o "${TMP}/out.json" mystery-devel 2> "${TMP}/err"; then
  echo "FAIL ${lock[0]} lock --lockfile exited non-zero:"
  cat "${TMP}/err"
  exit 1
fi

python3 - "${TMP}/out.json" <<'PY'
import json
import sys

names = [p["name"] for p in json.load(open(sys.argv[1]))["packages"]]
assert "mystery-devel" in names, names
assert "tour" not in names, names
PY
echo "ok   ${lock[0]##*/} lock resolves on top of a lockfile with assertions enabled"
