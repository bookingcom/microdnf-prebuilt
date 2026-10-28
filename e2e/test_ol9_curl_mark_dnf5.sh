#!/usr/bin/env bash
# The dnf5 twin of test_ol9_curl_mark.sh: rpm_mark(backend = "dnf5") and the standalone `dnf5 rpmdb-mark` write an rpmdb
# with curl and its dependency, and that rpmdb holds the same packages as the one the libdnf mark tool wrote.
set -euo pipefail

CURL_BIN="$1"
DNF5_RPMDB_DIR="$2"
LIBDNF_RPMDB_DIR="$3"
DNF5="$4"
CONF="$5"
REPO_DIR="$6"

source "$(dirname "$0")/dnf5_lib.sh"

echo "=== 1. Verifying Oracle Linux 9 curl binary extraction ==="
test -x "${CURL_BIN}"
file -L "${CURL_BIN}" | grep -q "ELF 64-bit"

# The package keys of an rpmdb: names, header digests and provides, one "table key" per line, sorted. Not the bytes of
# the database, which hold the install time.
keys() {
  python3 -I -c "
import sqlite3, sys
con = sqlite3.connect('file:' + sys.argv[1] + '?mode=ro', uri=True)
for table in ('Name', 'Sha1header', 'Providename'):
    for (key,) in con.execute('SELECT DISTINCT key FROM ' + table):
        print(table, key)
" "$1" | sort
}

echo "=== 2. rpm_mark(backend = dnf5) generated an rpmdb with curl and libcurl ==="
SQLITE_DB="${DNF5_RPMDB_DIR}/var/lib/rpm/rpmdb.sqlite"
test -f "${SQLITE_DB}"
keys "${SQLITE_DB}" | grep -qx 'Name curl'
keys "${SQLITE_DB}" | grep -qx 'Name libcurl'

echo "=== 3. It holds the same packages as the libdnf mark tool's rpmdb ==="
diff <(keys "${LIBDNF_RPMDB_DIR}/var/lib/rpm/rpmdb.sqlite") <(keys "${SQLITE_DB}")

echo "=== 4. Standalone dnf5 rpmdb-mark on a staging root, unprivileged ==="
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${WORK}/root"
dnf5_preamble "${CONF}" "${REPO_DIR}" "${WORK}"
STAGING_ROOT="${WORK}/multistage_docker_image"
"${DNF5}" "${PREAMBLE[@]}" rpmdb-mark --rpmdb-root="${STAGING_ROOT}" --archlist=x86_64 "curl=7.76.1-40.el9_8.8"
diff <(keys "${SQLITE_DB}") <(keys "${STAGING_ROOT}/var/lib/rpm/rpmdb.sqlite")

echo "=== 5. --nodeps marks the named package alone, a plain name finds the newest ==="
"${DNF5}" "${PREAMBLE[@]}" rpmdb-mark --rpmdb-root="${WORK}/nodeps" --archlist=x86_64 --nodeps curl
keys "${WORK}/nodeps/var/lib/rpm/rpmdb.sqlite" | grep -qx 'Name curl'
! keys "${WORK}/nodeps/var/lib/rpm/rpmdb.sqlite" | grep -qx 'Name libcurl'

echo "=== 6. An unknown package fails and writes no database ==="
if "${DNF5}" "${PREAMBLE[@]}" rpmdb-mark --rpmdb-root="${WORK}/none" --archlist=x86_64 nosuchpkg; then
  echo "Error: marking an unknown package succeeded"
  exit 1
fi
test ! -e "${WORK}/none/var/lib/rpm/rpmdb.sqlite"

echo "ALL TESTS PASSED SUCCESSFULLY!"
