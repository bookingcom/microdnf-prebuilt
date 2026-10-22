#!/usr/bin/env bash
set -euo pipefail

LOCKFILE="$1"

if [ ! -f "${LOCKFILE}" ]; then
  echo "Error: Lockfile ${LOCKFILE} does not exist"
  exit 1
fi

echo "Verifying lockfile is valid JSON..."
python3 -m json.tool "${LOCKFILE}" > /dev/null

echo "Verifying lockfile ends with an empty line (trailing newline)..."
LAST_BYTE="$(tail -c 1 "${LOCKFILE}")"
if [ -n "${LAST_BYTE}" ]; then
  echo "Error: Lockfile is missing a trailing newline at the end!"
  exit 1
fi

echo "Verifying URLs are relative to workspace and do not contain user home directory..."
if grep '"url"' "${LOCKFILE}" | grep -q '/home/'; then
  echo "Error: Found user home directory in URL:"
  grep '"url"' "${LOCKFILE}"
  exit 1
fi

echo "Verifying exclusion of mystery-devel..."
if grep -q '"mystery-devel"' "${LOCKFILE}"; then
  echo "Error: Excluded package mystery-devel found in lockfile"
  exit 1
fi

echo "Verifying tour package is present and marked as requested..."
if ! grep -q '"tour"' "${LOCKFILE}"; then
  echo "Error: tour package not found in lockfile"
  exit 1
fi

if ! grep -E '"requested":\s*true' "${LOCKFILE}" > /dev/null; then
  echo "Error: 'requested': true not found for requested package"
  exit 1
fi

echo "rpm_lock verification passed successfully!"
