#!/usr/bin/env bash
# `dnf5 lock` option tests against the synthetic "features" repository:
# --excludepkgs, --module-enable, --best/--nobest, --forcearch and equal-priority
# repos that carry the same packages, and weak dependencies.
# Args: <dnf5> <repomd.xml> <dnf.conf of a dnf_config>
# The cases are written as the flags of the removed dnf_lock tool; run_lock translates them to dnf5 options.
set -euo pipefail

DNF5="$1"
REPOMD="$2"          # .../features-repo/repodata/repomd.xml
DNF_CONF="$3"
source "$(dirname "$0")/dnf5_lib.sh"

abspath() { (cd "$(dirname "$1")" && echo "$(pwd -P)/$(basename "$1")"); }
REPO_DIR="$(dirname "$(dirname "$(abspath "${REPOMD}")")")"

WORK="${TEST_TMPDIR:-$(mktemp -d)}/lock_features"
rm -rf "${WORK}"
mkdir -p "${WORK}/repos.d"
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
EXPAT_NEW="expat-2.7.3-1.el10_2.7.x86_64"
MYSQL_MODULAR="mysql-8.4.7-1.module+el9.7.0+90001+e2e00084.x86_64"

FAILURES=0
OUT="${WORK}/out.json"
ERR="${WORK}/err.txt"

# `dnf5 lock` under the dnf_config preamble. The flags of the cases map as follows: --excludepkgs is dnf5's --exclude,
# the old --exclude is `lock --prune`, --nobest is --no-best, --reposdir is --setopt=reposdir=, and the solve
# architecture (--forcearch, else the first non-noarch of ARCHLIST) is --forcearch.
run_lock() {
  local globals=() lockargs=() forcearch="" a
  for a in "$@"; do
    case "${a}" in
      --excludepkgs=*) globals+=(--exclude="${a#*=}") ;;
      --exclude=*) lockargs+=(--prune="${a#*=}") ;;
      --nobest) globals+=(--no-best) ;;
      --best | --setopt=* | --setvar=*) globals+=("${a}") ;;
      --reposdir=*) globals+=(--setopt=reposdir="${a#*=}") ;;
      --forcearch=*) forcearch="${a#*=}" ;;
      *) lockargs+=("${a}") ;;
    esac
  done
  # A section that sets REPOSDIR (the archrepo and duplicate-repo cases) swaps the repositories, like the
  # former --reposdir; the default one is the features repository of the dnf_config.
  if [ "${REPOSDIR}" != "${WORK}/repos.d" ]; then globals+=(--setopt=reposdir="${REPOSDIR}"); fi
  if [ -z "${forcearch}" ]; then
    forcearch="$(tr , '\n' <<<"${ARCHLIST}" | grep -vx noarch | head -n 1)"
  fi
  rm -rf "${WORK}/dnf5tmp"
  mkdir -p "${WORK}/dnf5tmp/root"
  dnf5_preamble "${DNF_CONF}" "${REPO_DIR}" "${WORK}/dnf5tmp"
  # ${a[@]+"${a[@]}"}: an empty array is unbound under set -u in bash 3.2 (macOS).
  "${DNF5}" "${PREAMBLE[@]}" --forcearch="${forcearch}" ${globals[@]+"${globals[@]}"} \
    lock --archlist="${ARCHLIST}" -o "${OUT}" ${lockargs[@]+"${lockargs[@]}"} 2>"${ERR}"
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

# expect_lock "description" "expected sorted NEVRAs" case-args...
expect_lock() {
  local desc="$1" expected="$2"
  shift 2
  if ! run_lock "$@"; then
    fail "${desc}: dnf5 lock exited non-zero" "$(cat "${ERR}")"
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

# expect_error "description" "stderr substring" case-args...
expect_error() {
  local desc="$1" needle="$2"
  shift 2
  if run_lock "$@"; then
    fail "${desc}: dnf5 lock succeeded, expected an error"
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
  local conflict='"--no-best" not allowed together with named argument "--best"'
  expect_error "--best and --nobest conflict" "${conflict}" \
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

section_duprepos() {
  echo "--- the same NEVRA in two enabled repos of equal priority"
  # Two repos on the same metadata: every package exists once per repo, the way
  # appstream and baseos can both carry libtool-ltdl. The solver must install one
  # of them, not fail with "cannot install both X from repoA and X from repoB".
  mkdir -p "${WORK}/duprepos.d"
  cat > "${WORK}/duprepos.d/dup.repo" <<REPO
[dup-a]
name=dup-a
baseurl=file://${REPO_DIR}
enabled=1
gpgcheck=0

[dup-b]
name=dup-b
baseurl=file://${REPO_DIR}
enabled=1
gpgcheck=0
REPO
  REPOSDIR="${WORK}/duprepos.d" expect_lock "an identical package in two repos is locked once" \
    "libfoo-2.0-1.noarch" libfoo
  REPOSDIR="${WORK}/duprepos.d" expect_lock "its dependencies resolve across both repos" \
    "${CA_NEW}" certuser
  REPOSDIR="${WORK}/duprepos.d" expect_lock "--nobest locks it once as well" \
    "libfoo-2.0-1.noarch" libfoo --nobest
  REPOSDIR="${WORK}/duprepos.d" expect_lock "the newest candidate still wins with two repos" \
    "${EXPAT_NEW}" expat --forcearch=x86_64
}

section_weakdeps() {
  echo "--- weak dependencies"
  local weak_both="weakapp-1.0-1.noarch weakdep-1.0-1.noarch"
  # dnf with install_weak_deps=True (the EL default) installs the Recommends of what
  # it installs; dnf5 lock passes no flag for it, so this is libsolv's own default.
  expect_lock "Recommends are installed and locked, as dnf does" "${weak_both}" weakapp
  # dnf --setopt=install_weak_deps=<bool>: OptionBool takes 0/1, false/true, False/True, no/yes, off/on.
  for off in False false 0 no off; do
    expect_lock "--setopt=install_weak_deps=${off} leaves Recommends out, like dnf" \
      "weakapp-1.0-1.noarch" weakapp --setopt=install_weak_deps="${off}"
  done
  for on in True 1; do
    expect_lock "--setopt=install_weak_deps=${on} keeps them, like dnf" "${weak_both}" weakapp --setopt=install_weak_deps="${on}"
  done
  # dnf --exclude drops the excluded package, never one that is requested.
  expect_lock "--exclude still follows Recommends of kept packages" "${weak_both}" weakapp --exclude=nothing-matches
  expect_lock "--exclude trims an excluded Recommends" "weakapp-1.0-1.noarch" weakapp --exclude=weakdep
  expect_lock "a Recommends that is requested too stays" "${weak_both}" weakapp weakdep --exclude=weakdep

  # Supplements are reverse weak dependencies: dnf (libsolv) installs suppapp
  # because suppbase is being installed, though suppbase says nothing about it.
  local supp_both="suppapp-1.0-1.noarch suppbase-1.0-1.noarch"
  expect_lock "Supplements pull the supplementing package in" "${supp_both}" suppbase
  expect_lock "--exclude keeps a Supplements package whose target is kept" "${supp_both}" suppbase --exclude=nothing-matches
  expect_lock "--exclude trims an excluded Supplements package" "suppbase-1.0-1.noarch" suppbase --exclude=suppapp
  expect_lock "a Supplements package that is requested stays" "${supp_both}" suppbase suppapp --exclude=suppapp
  expect_lock "--setopt=install_weak_deps=False skips Supplements too" \
    "suppbase-1.0-1.noarch" suppbase --setopt=install_weak_deps=False

  # Rich dependencies: rpm:entry names such as "(a if b)" are parsed by libsolv; the
  # trim finds the edges through a provides query on the same dependency.
  local rich_rec="richapp-1.0-1.noarch richdep-1.0-1.noarch richtrigger-1.0-1.noarch"
  expect_lock "a conditional Recommends is installed when its condition holds" "${rich_rec}" richapp richtrigger
  expect_lock "--exclude keeps what a conditional Recommends installed" "${rich_rec}" richapp richtrigger --exclude=nothing-matches
  expect_lock "--exclude trims an excluded conditional Recommends" \
    "richapp-1.0-1.noarch richtrigger-1.0-1.noarch" richapp richtrigger --exclude=richdep
  local rich_supp="richbase-1.0-1.noarch richsupp-1.0-1.noarch richtrigger-1.0-1.noarch"
  expect_lock "a boolean Supplements is installed when both operands are" "${rich_supp}" richbase richtrigger
  expect_lock "--exclude keeps what a boolean Supplements installed" "${rich_supp}" richbase richtrigger --exclude=nothing-matches
  expect_lock "--exclude trims an excluded boolean Supplements package" \
    "richbase-1.0-1.noarch richtrigger-1.0-1.noarch" richbase richtrigger --exclude=richsupp
}

# Each shard runs every TEST_TOTAL_SHARDS-th section; unsharded runs take them all.
SECTIONS=(best excludepkgs modules basearch duprepos weakdeps)
if [ -n "${TEST_SHARD_STATUS_FILE:-}" ]; then touch "${TEST_SHARD_STATUS_FILE}"; fi
for i in "${!SECTIONS[@]}"; do
  if [ $((i % ${TEST_TOTAL_SHARDS:-1})) -eq "${TEST_SHARD_INDEX:-0}" ]; then
    "section_${SECTIONS[i]}"
  fi
done

if [ "${FAILURES}" -ne 0 ]; then
  echo "${FAILURES} dnf5 lock feature check(s) failed"
  exit 1
fi
echo "dnf5 lock feature tests passed"
