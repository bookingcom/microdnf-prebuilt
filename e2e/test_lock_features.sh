#!/usr/bin/env bash
# dnf_lock option tests against the synthetic "features" repository:
# --forcearch (and --archlist) solve architecture and $basearch.
set -euo pipefail

DNF_LOCK="$1"
REPOMD="$2"          # .../features-repo/repodata/repomd.xml

abspath() { (cd "$(dirname "$1")" && echo "$(pwd -P)/$(basename "$1")"); }
REPO_DIR="$(dirname "$(dirname "$(abspath "${REPOMD}")")")"

WORK="${TEST_TMPDIR:-$(mktemp -d)}/lock_features"
rm -rf "${WORK}"
mkdir -p "${WORK}/repos.d" "${WORK}/cache"
cat > "${WORK}/repos.d/features.repo" <<REPO
[features]
name=features
baseurl=file://${REPO_DIR}
enabled=1
gpgcheck=0
REPO
REPOSDIR="${WORK}/repos.d"
ARCHLIST=x86_64,noarch

FAILURES=0
OUT="${WORK}/out.json"
ERR="${WORK}/err.txt"

run_lock() {
  "${DNF_LOCK}" --reposdir="${REPOSDIR}" --setopt=cachedir="${WORK}/cache" \
    --releasever=9 --archlist="${ARCHLIST}" -o "${OUT}" "$@" 2>"${ERR}"
}

nevras() {
  python3 -c 'import json,sys; print(" ".join(sorted(p["nevra"] for p in json.load(open(sys.argv[1]))["packages"])))' "${OUT}"
}

# fail "message" [detail lines...]
fail() {
  echo "FAIL: $1"
  shift
  [ "$#" -eq 0 ] || printf '  %s\n' "$@"
  FAILURES=$((FAILURES + 1))
}

# expect_lock "description" "expected sorted NEVRAs" dnf_lock-args...
expect_lock() {
  local desc="$1" expected="$2"
  shift 2
  if ! run_lock "$@"; then
    fail "${desc}: dnf_lock exited non-zero" "$(cat "${ERR}")"
    return
  fi
  local got
  got="$(nevras)"
  if [ "${got}" != "${expected}" ]; then
    fail "${desc}" "expected: ${expected}" "got:      ${got}"
  else
    echo "ok: ${desc}"
  fi
}

section_basearch() {
  echo "--- --forcearch / --archlist solve architecture and \$basearch"
  # A repo whose baseurl holds $basearch, served from a directory named after an
  # architecture that is NOT the host's: only works when the solve arch feeds $basearch.
  case "$(uname -m)" in
    x86_64 | amd64) OTHER_ARCH=aarch64 ;;
    *) OTHER_ARCH=x86_64 ;;
  esac
  mkdir -p "${WORK}/archrepo" "${WORK}/archrepos.d"
  ln -sfn "${REPO_DIR}" "${WORK}/archrepo/${OTHER_ARCH}"
  cat > "${WORK}/archrepos.d/arch.repo" <<REPO
[archrepo]
name=archrepo
baseurl=file://${WORK}/archrepo/\$basearch
enabled=1
gpgcheck=0
REPO
  expect_lock "--forcearch=${OTHER_ARCH} sets \$basearch in the repo URL" \
    "libfoo-2.0-1.noarch" libfoo --reposdir="${WORK}/archrepos.d" --forcearch="${OTHER_ARCH}"
  REPOSDIR="${WORK}/archrepos.d" ARCHLIST="${OTHER_ARCH},noarch" \
    expect_lock "--archlist=${OTHER_ARCH} sets \$basearch in the repo URL" "libfoo-2.0-1.noarch" libfoo
  # An x86_64 package must resolve on any host with --forcearch=x86_64.
  expect_lock "--forcearch=x86_64 resolves x86_64 packages on any host" \
    "expat-2.7.3-1.el10_2.7.x86_64" expat --forcearch=x86_64
}

# Each shard runs every TEST_TOTAL_SHARDS-th section; unsharded runs take them all.
SECTIONS=(basearch)
if [ -n "${TEST_SHARD_STATUS_FILE:-}" ]; then touch "${TEST_SHARD_STATUS_FILE}"; fi
for i in "${!SECTIONS[@]}"; do
  if [ $((i % ${TEST_TOTAL_SHARDS:-1})) -eq "${TEST_SHARD_INDEX:-0}" ]; then
    "section_${SECTIONS[i]}"
  fi
done

if [ "${FAILURES}" -ne 0 ]; then
  echo "${FAILURES} dnf_lock feature check(s) failed"
  exit 1
fi
echo "dnf_lock feature tests passed"
