#!/usr/bin/env bash
# Runs dnf_lock in the target configuration, where libdnf's assertions are compiled in as in the release
# binaries (rpm_lock actions use the exec configuration, which defines NDEBUG).
# Args: <dnf_lock> <test.repo> <repomd.xml> <lockfile of the layer below>
#   or: <dnf5> <test.repo> <repomd.xml> <lockfile of the layer below> dnf5 <dnf.conf of a dnf_config>
set -euo pipefail

DNF_LOCK="$1"
REPO_CONF="$2"
REPOMD="$3"
BASE_LOCK="$4"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT
mkdir -p "${TMP}/repos.d"
REPO_DIR="$(cd "$(dirname "${REPOMD}")/.." && pwd)"

if [ "${5:-}" = dnf5 ]; then
  source "$(dirname "$0")/dnf5_lib.sh"
  mkdir -p "${TMP}/root"
  dnf5_preamble "$6" "${REPO_DIR}" "${TMP}"
  lock=("${DNF_LOCK}" "${PREAMBLE[@]}" lock)
else
  sed "s|@REPO_DIR@|${REPO_DIR}|" "${REPO_CONF}" > "${TMP}/repos.d/test.repo"
  lock=("${DNF_LOCK}" --reposdir="${TMP}/repos.d" --setopt=cachedir="${TMP}/cache" --releasever=9)
fi

if ! "${lock[@]}" --archlist=x86_64,noarch --lockfile="${BASE_LOCK}" -o "${TMP}/out.json" mystery-devel 2> "${TMP}/err"; then
  echo "FAIL ${lock[0]} --lockfile exited non-zero:"
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
echo "ok   ${lock[0]##*/} resolves on top of a lockfile with assertions enabled"
