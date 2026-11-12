#!/usr/bin/env bash
# dnf_lock option tests against the synthetic "features" repository:
# --excludepkgs, --module-enable, --best/--nobest and --forcearch.
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
CA_OLD="ca-certificates-2025.2.80_v9.0.305-1.el9.noarch certuser-1.0-1.noarch"
CA_NEW="ca-certificates-2026.2.92_v9.0.319-1.el9.noarch certuser-1.0-1.noarch"
EXPAT_OLD="expat-2.7.2-1.el10.x86_64"
MYSQL_MODULAR="mysql-8.4.7-1.module+el9.7.0+90001+e2e00084.x86_64"

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

# expect_error "description" "stderr substring" dnf_lock-args...
expect_error() {
  local desc="$1" needle="$2"
  shift 2
  if run_lock "$@"; then
    fail "${desc}: dnf_lock succeeded, expected an error"
  elif ! grep -qF -- "${needle}" "${ERR}"; then
    fail "${desc}: error does not mention '${needle}'" "$(cat "${ERR}")"
  else
    echo "ok: ${desc}"
  fi
}

section_best() {
  echo "--- best / nobest"
  expect_error "best (default) fails when the newest candidate is broken" \
    "nothing provides missing-lib needed by bestapp-2.0-1.noarch" bestapp
  expect_error "--best is explicit default" "missing-lib" bestapp --best
  expect_lock "--nobest falls back to an older candidate" "bestapp-1.0-1.noarch" bestapp --nobest
  expect_lock "--setopt=best=False falls back too" "bestapp-1.0-1.noarch" bestapp --setopt=best=False
  expect_error "--nobest keeps a pinned version (no fallback to bestapp-1.0)" \
    "missing-lib" bestapp-2.0-1.noarch --nobest
  expect_error "--best and --nobest conflict" "Argument --nobest is not allowed with argument --best" \
    bestapp --best --nobest
  expect_lock "--nobest still takes the newest installable candidate" \
    "${CA_NEW}" certuser --nobest
}

section_excludepkgs() {
  echo "--- excludepkgs"
  expect_lock "solver sees the newest ca-certificates by default" \
    "${CA_NEW}" certuser
  expect_lock "--excludepkgs glob hides the release cut-off" \
    "${CA_OLD}" \
    certuser --excludepkgs='ca-certificates-2026.2.92_v9.0.319-*'
  expect_lock "--setopt=excludepkgs= glob" \
    "${CA_OLD}" \
    certuser --setopt='excludepkgs=ca-certificates-2026.2.92_v9.0.319-*'
  expect_lock "--setopt=<repo>.excludepkgs= glob" \
    "${CA_OLD}" \
    certuser --setopt='features.excludepkgs=ca-certificates-2026.2.92_v9.0.319-*'
  expect_lock "--excludepkgs NEVRA without arch" "${EXPAT_OLD}" expat --excludepkgs=expat-2.7.3-1.el10_2.7
  expect_lock "--excludepkgs list, matching glob first" "${EXPAT_OLD}" \
    expat --excludepkgs=expat-2.7.3-1.el10_2.7,nothing-matches
  expect_lock "--excludepkgs repeated, matching flag first" "${EXPAT_OLD}" \
    expat --excludepkgs=expat-2.7.3-1.el10_2.7 --excludepkgs=nothing-matches
  expect_lock "--excludepkgs repeated, matching flag last" "${EXPAT_OLD}" \
    expat --excludepkgs=nothing-matches --excludepkgs=expat-2.7.3-1.el10_2.7
  expect_error "excluding every candidate leaves nothing to lock" "No packages matched" \
    expat --excludepkgs='expat-*'
}

section_modules() {
  echo "--- modules (mysql:8.4 is modular, mysql 8.0 is not)"
  expect_lock "modular packages are hidden by default" "mysql-8.0.44-1.el9.x86_64" \
    mysql --setopt=module_platform_id=platform:el9
  expect_lock "--module-enable exposes the stream" \
    "${MYSQL_MODULAR}" \
    mysql --module-enable=mysql:8.4 --setopt=module_platform_id=platform:el9
  expect_lock "--module-enable repeated and comma-separated is deduplicated" \
    "${MYSQL_MODULAR}" \
    mysql --module-enable=mysql:8.4,mysql:8.4 --module-enable=mysql:8.4 --setopt=module_platform_id=platform:el9
  expect_error "--module-enable of an unknown stream fails" "mysql:9.9" \
    mysql --module-enable=mysql:9.9 --setopt=module_platform_id=platform:el9
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
SECTIONS=(best excludepkgs modules basearch)
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
