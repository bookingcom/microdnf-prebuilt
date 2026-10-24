#!/usr/bin/env bash
# Regenerates e2e/testdata/installroot/var/lib/rpm/rpmdb.sqlite, the seeded
# rpmdb used by the `dnf_lock --installroot` e2e tests.
#
# Builds metadata-only RPMs for the packages "installed" in the parent image
# and records them with `rpm --justdb` in a sqlite rpmdb (EL9 default backend).
# Needs a container runtime and an EL9 image whose repositories ship rpm-build:
#
#   IMAGE=oraclelinux:9 REPOSDIR=/path/to/repos.d e2e/testdata/make_installroot_rpmdb.sh
set -euo pipefail

IMAGE="${IMAGE:-oraclelinux:9}"
OUT="$(cd "$(dirname "$0")" && pwd)/installroot/var/lib/rpm"
mkdir -p "${OUT}"

repos_args=()
if [ -n "${REPOSDIR:-}" ]; then
  repos_args=(-v "${REPOSDIR}:/repos:ro" -e DNF_REPOSDIR=/repos)
fi

docker run --rm --platform linux/amd64 ${repos_args[@]+"${repos_args[@]}"} -v "${OUT}:/out" "${IMAGE}" bash -euo pipefail -c '
dnf_args=(-y -q)
if [ -n "${DNF_REPOSDIR:-}" ]; then dnf_args+=(--setopt=reposdir="${DNF_REPOSDIR}"); fi
dnf "${dnf_args[@]}" install rpm-build >/dev/null

mkspec() { # name version extra-tags...
  local name="$1" ver="$2"; shift 2
  {
    echo "Name: ${name}"
    echo "Version: ${ver}"
    echo "Release: 1"
    echo "Summary: ${name} test package"
    echo "License: MIT"
    echo "BuildArch: noarch"
    for tag in "$@"; do echo "${tag}"; done
    echo "%description"
    echo "${name} installed in the dnf_lock e2e installroot."
    echo "%files"
  } > "/tmp/${name}.spec"
  rpmbuild -bb --quiet --define "_topdir /tmp/rpmbuild" "/tmp/${name}.spec"
}

mkspec libfoo 1.0
mkspec oldtool 1.0 "Recommends: optional-x"
mkspec rich-cond 1.0
mkspec condhost 1.0 "Requires: (cond-extra if cond-trigger)"
mkspec dgtool 2.0

root=/tmp/root
mkdir -p "${root}"
rpm --root "${root}" --initdb
rpm --root "${root}" -i --justdb --nodeps --noscripts --notriggers /tmp/rpmbuild/RPMS/noarch/*.rpm
rpm --root "${root}" -qa | sort
rm -f /out/rpmdb.sqlite*
cp "${root}/var/lib/rpm/rpmdb.sqlite" /out/rpmdb.sqlite
'
ls -l "${OUT}"
