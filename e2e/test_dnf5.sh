#!/usr/bin/env bash
# Smoke test: dnf5 loads a local repository, lists packages and resolves a transaction inside a temporary installroot.
set -euo pipefail

DNF5="$1"
REPO_DIR="$(cd "$2" && pwd)"
NOARCH_RPM="$(cd "$(dirname "$3")" && pwd)/$(basename "$3")"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${WORK}/repos" "${WORK}/root/etc/dnf/libdnf5-plugins" "${WORK}/rpmconfig"

# rpm runs <config dir>/init.lua when it starts; dnf5 must not take the host's.
printf 'io.open("%s/lua-ran", "w"):close()\n' "${WORK}" >| "${WORK}/rpmconfig/init.lua"

# A plugin configuration for a plugin that is not built in (as the rhsm one on a RHEL-like image) must be skipped, not fatal.
printf '[main]\nname=rhsm\nenabled=1\n' >| "${WORK}/root/etc/dnf/libdnf5-plugins/rhsm.conf"

cat >| "${WORK}/repos/ol9.repo" <<EOF
[ol9]
name=ol9
baseurl=file://${REPO_DIR}
enabled=1
gpgcheck=0
repo_gpgcheck=0
EOF

opts=(
  --installroot="${WORK}/root"
  --forcearch=x86_64
  --releasever=9
  --setopt=reposdir="${WORK}/repos"
  --setopt=cachedir="${WORK}/cache"
  --setopt=logdir="${WORK}/log"
)

echo "=== repoquery --available ==="
rc=0
available="$(RPM_CONFIGDIR="${WORK}/rpmconfig" "${DNF5}" repoquery --available "${opts[@]}" 2>"${WORK}/repoquery.err")" || rc=$?
cat "${WORK}/repoquery.err"
echo "exit code: ${rc}"
test "${rc}" -eq 0
grep -q 'Skipping libdnf plugin "rhsm".*not built in' "${WORK}/log/dnf5.log"
echo "${available}"
grep -q '^curl-0:7.76.1-40.el9_8.8.x86_64$' <<<"${available}"

echo "=== install --assumeno (dependencies are absent in the installroot, so the solver must report them) ==="
rc=0
out="$("${DNF5}" install --assumeno curl "${opts[@]}" 2>&1)" || rc=$?
echo "${out}"
echo "exit code: ${rc}"
# The solver cannot satisfy the glibc requirement, which is an error (1), not a crash (>= 128) or success.
test "${rc}" -eq 1
grep -q 'nothing provides' <<<"${out}"

echo "=== the host's rpm Lua is not run ==="
test ! -e "${WORK}/lua-ran"

echo "=== a noarch package passes rpm's arch, os and digest checks ==="
# The transaction itself needs chroot(2), so it succeeds only as root. Unprivileged it stops at that step, which
# comes after the checks that used to reject the package (an empty rpmrc has no arch_compat entries).
rc=0
out="$("${DNF5}" install --assumeyes --no-gpgchecks "${NOARCH_RPM}" --installroot="${WORK}/root" --releasever=9 \
  --setopt=reposdir="${WORK}/repos" --setopt=cachedir="${WORK}/cache" --setopt=logdir="${WORK}/log" 2>&1)" || rc=$?
echo "${out}"
echo "exit code: ${rc}"
if grep -qE 'different (architecture|operating system)|no signature' <<<"${out}"; then
  echo "rpm rejected the package" >&2
  exit 1
fi
if [[ "${rc}" -ne 0 ]]; then
  grep -q 'Unable to change root directory' "${WORK}/log/dnf5.log"
fi

echo "=== rpmdb is created inside the installroot ==="
test -f "${WORK}/root/var/lib/rpm/rpmdb.sqlite"

echo "=== the lock command is linked in ==="
help="$("${DNF5}" lock --help)"
grep -q -- "--archlist" <<<"${help}"
grep -q -- "--module-enable" <<<"${help}"

echo "dnf5 smoke test passed"
