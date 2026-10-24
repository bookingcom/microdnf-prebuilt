#!/usr/bin/env bash
# dnf_lock feature tests against the synthetic "features" repository:
# --installroot, excludepkgs, module streams, best/nobest, rich and weak deps.
set -euo pipefail

DNF_LOCK="$1"
REPOMD="$2"          # .../features-repo/repodata/repomd.xml
INSTALLROOT_FILE="$3" # .../installroot/var/lib/rpm/rpmdb.sqlite

abspath() { (cd "$(dirname "$1")" && echo "$(pwd -P)/$(basename "$1")"); }
REPO_DIR="$(dirname "$(dirname "$(abspath "${REPOMD}")")")"
INSTALLROOT_SRC="$(dirname "$(dirname "$(dirname "$(dirname "$(abspath "${INSTALLROOT_FILE}")")")")")"

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

# A snapshot and a mirror: two repos with the same builds and the same priority.
mkdir -p "${WORK}/repos-dup.d"
for id in snapshot mirror; do
  printf '[%s]\nname=%s\nbaseurl=file://%s\nenabled=1\ngpgcheck=0\n' "${id}" "${id}" "${REPO_DIR}" \
    >>"${WORK}/repos-dup.d/dup.repo"
done
REPOSDIR="${WORK}/repos.d"

# Writable copy of the parent image root: librpm opens the sqlite rpmdb in WAL mode.
fresh_installroot() {
  rm -rf "${WORK}/installroot"
  cp -RL "${INSTALLROOT_SRC}" "${WORK}/installroot"
  chmod -R u+w "${WORK}/installroot"
  echo "${WORK}/installroot"
}

FAILURES=0
OUT="${WORK}/out.json"
ERR="${WORK}/err.txt"

run_lock() {
  "${DNF_LOCK}" --reposdir="${REPOSDIR}" --setopt=cachedir="${WORK}/cache" \
    --releasever=9 --archlist=x86_64,noarch -o "${OUT}" "$@" 2>"${ERR}"
}

nevras() {
  python3 -c 'import json,sys; print(" ".join(sorted(p["nevra"] for p in json.load(open(sys.argv[1]))["packages"])))' "${OUT}"
}

# expect_lock "description" "expected sorted NEVRAs" dnf_lock-args...
expect_lock() {
  local desc="$1" expected="$2"
  shift 2
  if ! run_lock "$@"; then
    echo "FAIL: ${desc}: dnf_lock exited non-zero"
    cat "${ERR}"
    FAILURES=$((FAILURES + 1))
    return
  fi
  local got
  got="$(nevras)"
  if [ "${got}" != "${expected}" ]; then
    echo "FAIL: ${desc}"
    echo "  expected: ${expected}"
    echo "  got:      ${got}"
    FAILURES=$((FAILURES + 1))
  else
    echo "ok: ${desc}"
  fi
}

# expect_error "description" "stderr substring" dnf_lock-args...
expect_error() {
  local desc="$1" needle="$2"
  shift 2
  if run_lock "$@"; then
    echo "FAIL: ${desc}: dnf_lock succeeded, expected an error"
    FAILURES=$((FAILURES + 1))
  elif ! grep -qF -- "${needle}" "${ERR}"; then
    echo "FAIL: ${desc}: error does not mention '${needle}'"
    cat "${ERR}"
    FAILURES=$((FAILURES + 1))
  else
    echo "ok: ${desc}"
  fi
}

# expect_json "description" "python expression over d (the lockfile)"
expect_json() {
  if python3 -c "import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if ($2) else 1)" "${OUT}"; then
    echo "ok: $1"
  else
    echo "FAIL: $1"
    cat "${OUT}"
    FAILURES=$((FAILURES + 1))
  fi
}

# expect_stderr "description" "substring" -- checks the stderr of the last run.
expect_stderr() {
  if grep -qF -- "$2" "${ERR}"; then
    echo "ok: $1"
  else
    echo "FAIL: $1: stderr does not mention '$2'"
    cat "${ERR}"
    FAILURES=$((FAILURES + 1))
  fi
}

# expect_no_stderr "description" "substring" -- checks the stderr of the last run.
expect_no_stderr() {
  if grep -qF -- "$2" "${ERR}"; then
    echo "FAIL: $1: stderr mentions '$2'"
    cat "${ERR}"
    FAILURES=$((FAILURES + 1))
  else
    echo "ok: $1"
  fi
}

section_installroot() {
  echo "--- installroot (seeded rpmdb: libfoo-1.0, oldtool-1.0, rich-cond-1.0, condhost-1.0, dgtool-2.0)"
  expect_lock "default resolves the full closure" \
    "app-1.0-1.noarch libfoo-2.0-1.noarch oldtool-1.0-1.noarch optional-x-1.0-1.noarch" app
  expect_lock "installroot locks only the transaction, upgrading libfoo" \
    "app-1.0-1.noarch libfoo-2.0-1.noarch" app --installroot="$(fresh_installroot)"
  expect_json "upgrade records the installed package it replaces" \
    '[p.get("replaces") for p in d["packages"] if p["name"] == "libfoo"] == [["libfoo-1.0-1.noarch"]]'
  expect_json "requested lists the requested name" 'd["requested"] == ["app"]'
  expect_lock "already installed request is a no-op" "" oldtool --installroot="$(fresh_installroot)"
  expect_json "no-op lock still lists the request" 'd["requested"] == ["oldtool"] and d["packages"] == []'
  expect_lock "an installed package absent from the repos is a no-op" "" condhost --installroot="$(fresh_installroot)"
  expect_error "a request with no candidate fails like dnf install" "No match for argument: nosuchpkg" \
    app nosuchpkg --installroot="$(fresh_installroot)"
  expect_error "an installed-only package needs the installroot" "No match for argument: condhost" condhost
  expect_error "--nobest does not drop a request with no candidate" "No match for argument: nosuchpkg" \
    app nosuchpkg --nobest
  expect_lock "relative installroot path" \
    "app-1.0-1.noarch libfoo-2.0-1.noarch" app \
    --installroot="$(python3 -c 'import os,sys; print(os.path.relpath(sys.argv[1]))' "$(fresh_installroot)")"
  mkdir -p "${WORK}/scratchroot/usr"
  expect_lock "installroot without an rpmdb behaves like an empty image" \
    "app-1.0-1.noarch libfoo-2.0-1.noarch oldtool-1.0-1.noarch optional-x-1.0-1.noarch" \
    app --installroot="${WORK}/scratchroot"
  expect_stderr "installroot with usr/ but no rpmdb warns" "has no rpmdb (looked for var/lib/rpm/"
  local noos
  noos="$(fresh_installroot)"
  rm -f "${noos}/usr/lib/os-release"
  expect_lock "installroot without os-release still resolves" \
    "app-1.0-1.noarch libfoo-2.0-1.noarch" app --installroot="${noos}"
  expect_stderr "installroot without os-release warns about PLATFORM_ID" "PLATFORM_ID is unknown"
  expect_lock "module_platform_id stands in for os-release" \
    "app-1.0-1.noarch libfoo-2.0-1.noarch" app --installroot="${noos}" --setopt=module_platform_id=platform:el9
  expect_no_stderr "module_platform_id silences the os-release warning" "PLATFORM_ID is unknown"
  expect_lock "an installroot with os-release does not warn" \
    "app-1.0-1.noarch libfoo-2.0-1.noarch" app --installroot="$(fresh_installroot)"
  expect_no_stderr "an installroot with os-release does not warn" "PLATFORM_ID is unknown"
  expect_lock "conditional Requires of an installed package is kept" \
    "cond-extra-1.0-1.noarch cond-trigger-1.0-1.noarch" cond-trigger --installroot="$(fresh_installroot)"
  expect_lock "no installed package, no conditional Requires" \
    "cond-trigger-1.0-1.noarch" cond-trigger
}

section_downgrades() {
  echo "--- downgrades (dgtool-2.0 installed; the repo has 1.0 and 2.0)"
  expect_lock "an explicit older NEVRA downgrades the installed package" \
    "dgtool-1.0-1.noarch" dgtool-1.0-1.noarch --installroot="$(fresh_installroot)"
  expect_json "downgrade records the installed package it replaces" \
    '[p.get("replaces") for p in d["packages"]] == [["dgtool-2.0-1.noarch"]] and d["requested"] == ["dgtool"]'
  expect_lock "an explicit older version (no release) downgrades too" \
    "dgtool-1.0-1.noarch" dgtool-1.0 --installroot="$(fresh_installroot)"
  expect_lock "--nobest keeps the pinned older version" \
    "dgtool-1.0-1.noarch" dgtool-1.0-1.noarch --nobest --installroot="$(fresh_installroot)"
  expect_lock "a name-only request stays satisfied by the newer installed package" "" \
    dgtool --installroot="$(fresh_installroot)"
  expect_lock "an explicit request for the installed NEVRA is a no-op" "" \
    dgtool-2.0-1.noarch --installroot="$(fresh_installroot)"
  expect_lock "without an installroot the older NEVRA is a plain install" \
    "dgtool-1.0-1.noarch" dgtool-1.0-1.noarch
  expect_lock "a dependency pinned to an older build downgrades the installed package" \
    "dgtool-1.0-1.noarch dgtool-devel-1.0-1.noarch" dgtool-devel --installroot="$(fresh_installroot)"
  expect_json "the dependency downgrade records the installed package it replaces" \
    '[p.get("replaces") for p in d["packages"] if p["name"] == "dgtool"] == [["dgtool-2.0-1.noarch"]]'
  REPOSDIR="${WORK}/repos-dup.d"
  expect_lock "a build in two repos is one install, not two conflicting ones" \
    "app-1.0-1.noarch libfoo-2.0-1.noarch oldtool-1.0-1.noarch optional-x-1.0-1.noarch" app
  expect_lock "a build in two repos on an installroot" \
    "app-1.0-1.noarch libfoo-2.0-1.noarch" app --installroot="$(fresh_installroot)"
  expect_lock "a pinned older build in two repos still downgrades once" \
    "dgtool-1.0-1.noarch" dgtool-1.0-1.noarch --installroot="$(fresh_installroot)"
  REPOSDIR="${WORK}/repos.d"
  expect_lock "excludepkgs hiding the newer repo version does not downgrade the installed one" \
    "dgapp-1.0-1.noarch" dgapp --excludepkgs='dgtool-2*' --installroot="$(fresh_installroot)"
}

section_weak_deps() {
  echo "--- weak dependencies"
  expect_lock "Recommends and Supplements are installed by default" \
    "weak-rec-1.0-1.noarch weak-sup-1.0-1.noarch weakapp-1.0-1.noarch" weakapp
  expect_json "weak edges are exported (Recommends, reverse Supplements) and stay out of dependencies" \
    '{p["name"]: (p["dependencies"], p["weak_dependencies"]) for p in d["packages"]}["weakapp"] == ([], ["weak-rec", "weak-sup"]) and all(p["weak_dependencies"] == [] for p in d["packages"] if p["name"] != "weakapp")'
  expect_lock "install_weak_deps=False skips them" \
    "weakapp-1.0-1.noarch" weakapp --setopt=install_weak_deps=False
  expect_lock "weak deps survive --exclude pruning of unrelated packages" \
    "weak-rec-1.0-1.noarch weak-sup-1.0-1.noarch weakapp-1.0-1.noarch" weakapp --exclude=oldtool
  expect_lock "--exclude prunes a weak dependency" \
    "weak-sup-1.0-1.noarch weakapp-1.0-1.noarch" weakapp --exclude=weak-rec
  expect_lock "weak dep is installed without an installroot" \
    "newapp-1.0-1.noarch optional-x-1.0-1.noarch" newapp
  expect_lock "weak dep declined by the installed set is not pulled (exclude_from_weak_autodetect)" \
    "newapp-1.0-1.noarch" newapp --installroot="$(fresh_installroot)"
  expect_lock "exclude_from_weak_autodetect=False pulls it again" \
    "newapp-1.0-1.noarch optional-x-1.0-1.noarch" newapp --installroot="$(fresh_installroot)" \
    --setopt=exclude_from_weak_autodetect=False

  expect_lock "exclude_from_weak_autodetect accepts microdnf spellings (OFF)" \
    "newapp-1.0-1.noarch optional-x-1.0-1.noarch" newapp --installroot="$(fresh_installroot)" \
    --setopt=exclude_from_weak_autodetect=OFF
  expect_lock "exclude_from_weak_autodetect accepts microdnf spellings (Yes)" \
    "newapp-1.0-1.noarch" newapp --installroot="$(fresh_installroot)" \
    --setopt=exclude_from_weak_autodetect=Yes
  expect_error "exclude_from_weak_autodetect rejects a non-boolean" "exclude_from_weak_autodetect" \
    newapp --setopt=exclude_from_weak_autodetect=maybe
  expect_error "--setopt without a value is an error" "Missing value" bestapp --setopt=foo
}

section_rich_deps() {
  echo "--- rich dependencies"
  expect_lock "(a if b) without b, (b or c)" "rich-b-1.0-1.noarch richapp-1.0-1.noarch" richapp
  expect_lock "(a if b) with b requested" \
    "rich-a-1.0-1.noarch rich-b-1.0-1.noarch rich-cond-1.0-1.noarch richapp-1.0-1.noarch" richapp rich-cond
  expect_lock "(a if b) with b installed in the installroot" \
    "rich-a-1.0-1.noarch rich-b-1.0-1.noarch richapp-1.0-1.noarch" richapp --installroot="$(fresh_installroot)"
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
  expect_error "--best and --nobest conflict" "not allowed" bestapp --best --nobest
}

section_excludepkgs() {
  echo "--- excludepkgs"
  expect_lock "solver sees the newest ca-certificates by default" \
    "ca-certificates-2026.2.92_v9.0.319-1.el9.noarch certuser-1.0-1.noarch" certuser
  expect_lock "--excludepkgs glob hides the release cut-off" \
    "ca-certificates-2025.2.80_v9.0.305-1.el9.noarch certuser-1.0-1.noarch" \
    certuser --excludepkgs='ca-certificates-2026.2.92_v9.0.319-*'
  expect_lock "--setopt=excludepkgs= glob" \
    "ca-certificates-2025.2.80_v9.0.305-1.el9.noarch certuser-1.0-1.noarch" \
    certuser --setopt='excludepkgs=ca-certificates-2026.2.92_v9.0.319-*'
  expect_lock "--setopt=<repo>.excludepkgs= glob" \
    "ca-certificates-2025.2.80_v9.0.305-1.el9.noarch certuser-1.0-1.noarch" \
    certuser --setopt='features.excludepkgs=ca-certificates-2026.2.92_v9.0.319-*'
  expect_lock "--excludepkgs NEVRA without arch" "expat-2.7.2-1.el10.x86_64" expat --excludepkgs=expat-2.7.3-1.el10_2.7
  expect_lock "--excludepkgs list, matching glob first" "expat-2.7.2-1.el10.x86_64" \
    expat --excludepkgs=expat-2.7.3-1.el10_2.7,nothing-matches
  expect_lock "--excludepkgs repeated, matching flag first" "expat-2.7.2-1.el10.x86_64" \
    expat --excludepkgs=expat-2.7.3-1.el10_2.7 --excludepkgs=nothing-matches
  expect_error "excluding every candidate leaves nothing to lock" "No match for argument: expat" \
    expat --excludepkgs='expat-*'
}

section_modules() {
  echo "--- modules (mysql:8.4 is modular, mysql 8.0 is not)"
  expect_lock "modular packages are hidden by default" "mysql-8.0.44-1.el9.x86_64" mysql
  expect_lock "--module-enable with module_platform_id" \
    "mysql-8.4.7-1.module+el9.7.0+90001+e2e00084.x86_64" \
    mysql --module-enable=mysql:8.4 --setopt=module_platform_id=platform:el9
  expect_error "--module-enable needs a platform" "module(platform:el9)" mysql --module-enable=mysql:8.4
  # modules.d state of a root: its sorted listing with checksums, or "absent".
  modules_state() {
    if [ -d "$1/etc/dnf/modules.d" ]; then
      (cd "$1/etc/dnf/modules.d" && find . -type f -exec cksum {} + | sort)
    else
      echo absent
    fi
  }
  ROOT="$(fresh_installroot)"
  MODULES_BEFORE="$(modules_state "${ROOT}")"
  expect_lock "--module-enable with the installroot's os-release PLATFORM_ID" \
    "mysql-8.4.7-1.module+el9.7.0+90001+e2e00084.x86_64" \
    mysql --module-enable=mysql:8.4 --installroot="${ROOT}"
  if [ "$(modules_state "${ROOT}")" != "${MODULES_BEFORE}" ]; then
    echo "FAIL: --module-enable wrote module state into the installroot"
    FAILURES=$((FAILURES + 1))
  fi
  ROOT="$(fresh_installroot)"
  mkdir -p "${ROOT}/etc/dnf/modules.d"
  printf '[mysql]\nname=mysql\nstream=8.4\nprofiles=\nstate=enabled\n' > "${ROOT}/etc/dnf/modules.d/mysql.module"
  expect_lock "stream enabled in the installroot's modules.d" \
    "mysql-8.4.7-1.module+el9.7.0+90001+e2e00084.x86_64" mysql --installroot="${ROOT}"
  if [ -n "$(ls "${ROOT}/etc/dnf/modules.d" | grep -v '^mysql.module$' || true)" ]; then
    echo "FAIL: dnf_lock wrote module state into the installroot"
    FAILURES=$((FAILURES + 1))
  fi
}

section_basearch() {
  echo "--- \$basearch follows --forcearch / --archlist"
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
  run_lock_arch() {
    "${DNF_LOCK}" --reposdir="${WORK}/archrepos.d" --setopt=cachedir="${WORK}/cache" \
      --releasever=9 --archlist="${OTHER_ARCH},noarch" -o "${OUT}" "$@" 2>"${ERR}"
  }
  if run_lock_arch libfoo && [ "$(nevras)" = "libfoo-2.0-1.noarch" ]; then
    echo "ok: --archlist=${OTHER_ARCH} sets \$basearch in the repo URL"
  else
    echo "FAIL: --archlist=${OTHER_ARCH} sets \$basearch in the repo URL"
    cat "${ERR}"
    FAILURES=$((FAILURES + 1))
  fi
}

# Each shard runs every TEST_TOTAL_SHARDS-th section; unsharded runs take them all.
SECTIONS=(installroot downgrades weak_deps rich_deps best excludepkgs modules basearch)
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
