#!/usr/bin/env bash
# Smoke test of the dnf_config rule: dnf5 reads the generated dnf.conf and .repo file through the fixed preamble,
# lists the repository and runs the solver, touching nothing outside its scratch directory.
set -euo pipefail

DNF5="$1"
CONF="$2"
REPO_DIR="$3"

source "$(dirname "$0")/dnf5_lib.sh"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${WORK}/root"
dnf5_preamble "${CONF}" "${REPO_DIR}" "${WORK}"

echo "=== repoquery --available ==="
rc=0
available="$("${DNF5}" repoquery --available "${PREAMBLE[@]}" 2>"${WORK}/repoquery.err")" || rc=$?
cat "${WORK}/repoquery.err"
echo "exit code: ${rc}"
test "${rc}" -eq 0
echo "${available}"
grep -q '^curl-0:7.76.1-40.el9_8.8.x86_64$' <<<"${available}"

echo "=== install --assumeno (dependencies are absent in the installroot, so the solver must report them) ==="
rc=0
out="$("${DNF5}" install --assumeno curl "${PREAMBLE[@]}" 2>&1)" || rc=$?
echo "${out}"
echo "exit code: ${rc}"
test "${rc}" -eq 1
grep -q 'nothing provides' <<<"${out}"

echo "=== dnf.conf options take effect ==="
# install_weak_deps and best are set in the generated dnf.conf, not on the command line.
dumped="$("${DNF5}" "${PREAMBLE[@]}" --dump-main-config)"
grep -q '^best = 1$' <<<"${dumped}"
grep -q '^install_weak_deps = 0$' <<<"${dumped}"
grep -q '^excludepkgs = mystery-devel$' <<<"${dumped}"

echo "=== cache and log went to the scratch directory ==="
test -d "${WORK}/cache"
test -f "${WORK}/log/dnf5.log"

echo "dnf5 config smoke test passed"
