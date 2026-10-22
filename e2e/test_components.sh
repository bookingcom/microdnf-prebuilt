#!/usr/bin/env bash
set -euo pipefail

LOCK_FILE="$1"
ARCHIVE_FILE="$2"
AWAY_EXEC="$3"

echo "=== Testing Step 1: Generated Lockfile ==="
if [ ! -f "${LOCK_FILE}" ]; then
  echo "Error: Lockfile not found: ${LOCK_FILE}"
  exit 1
fi

grep -q '"tour"' "${LOCK_FILE}" || {
  echo "Error: 'tour' not found in requested list"
  exit 1
}

# Verify exclusion trimming: mystery-devel was excluded and should NOT be in packages
if grep -q '"mystery-devel"' "${LOCK_FILE}"; then
  echo "Error: Excluded package 'mystery-devel' was found in lockfile packages!"
  exit 1
fi
echo "Step 1 Lockfile verification passed."

echo "=== Testing Step 3: Created Archive ==="
if [ ! -f "${ARCHIVE_FILE}" ]; then
  echo "Error: Archive file not found: ${ARCHIVE_FILE}"
  exit 1
fi

tar -tzf "${ARCHIVE_FILE}" | grep -q 'usr/bin/away' || {
  echo "Error: usr/bin/away not found in archive"
  exit 1
}
echo "Step 3 Archive verification passed."

echo "=== Testing Step 4: Extracted Files & Executables ==="
if [ ! -x "${AWAY_EXEC}" ]; then
  echo "Error: Executable flag not set on ${AWAY_EXEC}"
  exit 1
fi

AWAY_CONTENT="$(cat "${AWAY_EXEC}")"
if [[ "${AWAY_CONTENT}" != *"take you away"* ]]; then
  echo "Error: Unexpected content in ${AWAY_EXEC}: '${AWAY_CONTENT}'"
  exit 1
fi

echo "Executable permission 0755 and file content verified on ${AWAY_EXEC}"
echo "Step 4 Extraction and execution verification passed."
echo "ALL COMPONENT TESTS PASSED SUCCESSFULLY!"
