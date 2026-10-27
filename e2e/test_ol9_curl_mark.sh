#!/usr/bin/env bash
set -euo pipefail

CURL_BIN="$1"
RPMDB_DIR="$2"
MARK_BIN="$3"
OL9_REPO="$4"
OL9_REPO_DIR="$5"

echo "=== 1. Verifying Oracle Linux 9 curl binary extraction ==="
if [ ! -f "${CURL_BIN}" ]; then
  echo "Error: curl binary not found: ${CURL_BIN}"
  exit 1
fi

if [ ! -x "${CURL_BIN}" ]; then
  echo "Error: curl binary is not executable: ${CURL_BIN}"
  exit 1
fi

# Verify ELF header
if ! file "${CURL_BIN}" | grep -q "ELF 64-bit"; then
  echo "Error: ${CURL_BIN} is not an ELF 64-bit binary"
  exit 1
fi
echo "Verified: ${CURL_BIN} is an executable ELF 64-bit binary from Oracle Linux 9."

echo "=== 2. Verifying rpm_mark generated rpmdb ==="
if [ ! -d "${RPMDB_DIR}" ]; then
  echo "Error: rpmdb directory not found: ${RPMDB_DIR}"
  exit 1
fi

SQLITE_DB="${RPMDB_DIR}/var/lib/rpm/rpmdb.sqlite"
if [ ! -f "${SQLITE_DB}" ]; then
  echo "Error: SQLite RPM DB not found at ${SQLITE_DB}"
  exit 1
fi
echo "Verified: rpmdb.sqlite exists at ${SQLITE_DB}."

echo "=== 3. Inspecting rpmdb contents (Sysdig / Syft scanner verification) ==="
python3 -c "
import sqlite3, sys

con = sqlite3.connect('${SQLITE_DB}')
cur = con.cursor()

names = [row[0] for row in cur.execute('SELECT DISTINCT key FROM Name')]
print('Installed packages in rpmdb:', names)

if 'curl' not in names:
    sys.exit('Error: curl package not found in rpmdb')

if 'libcurl' not in names:
    sys.exit('Error: libcurl (transitive dependency) not found in rpmdb')

# Verify package headers in Packages table
rows = cur.execute('SELECT count(*) FROM Packages').fetchone()
print(f'Total packages in Packages table: {rows[0]}')
if rows[0] < 2:
    sys.exit('Error: Expected at least 2 packages in Packages table')
"

echo "=== 4. Testing standalone 'mark' CLI tool on a staging multistage docker image ==="
STAGING_ROOT="${TEST_TMPDIR}/multistage_docker_image"
rm -rf "${STAGING_ROOT}"
mkdir -p "${STAGING_ROOT}/usr/bin"

# Step A: Add curl binary to image
cp "${CURL_BIN}" "${STAGING_ROOT}/usr/bin/curl"
chmod 0755 "${STAGING_ROOT}/usr/bin/curl"
echo "Added curl binary to ${STAGING_ROOT}/usr/bin/curl"

# Step B: Prepare repo config with absolute path
TMP_REPOS_DIR="${TEST_TMPDIR}/repos.d"
mkdir -p "${TMP_REPOS_DIR}"
sed "s|@REPO_DIR@|${PWD}/${OL9_REPO_DIR}|g" "${OL9_REPO}" > "${TMP_REPOS_DIR}/ol9.repo"

# Step C: Run mark tool to mark curl as installed into rpmdb
"${MARK_BIN}" \
  --reposdir="${TMP_REPOS_DIR}" \
  --installroot="${STAGING_ROOT}" \
  --releasever="9" \
  --archlist="x86_64" \
  "curl=7.76.1-40.el9_8.8"

# Step D: Verify rpmdb inside multistage image
STAGING_SQLITE="${STAGING_ROOT}/var/lib/rpm/rpmdb.sqlite"
if [ ! -f "${STAGING_SQLITE}" ]; then
  echo "Error: Staging rpmdb.sqlite not found at ${STAGING_SQLITE}"
  exit 1
fi

python3 -c "
import sqlite3, sys

con = sqlite3.connect('${STAGING_SQLITE}')
cur = con.cursor()

names = [row[0] for row in cur.execute('SELECT DISTINCT key FROM Name')]
print('Staging rpmdb packages:', names)

assert 'curl' in names, 'curl not found in staging rpmdb'
assert 'libcurl' in names, 'libcurl not found in staging rpmdb'
print('Verified: Oracle Linux 9 curl binary marked as installed with minimal transitive dependencies in staging rpmdb.')
"

echo "ALL TESTS PASSED SUCCESSFULLY!"
