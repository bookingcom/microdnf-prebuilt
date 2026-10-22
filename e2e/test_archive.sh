#!/usr/bin/env bash
set -euo pipefail

ARCHIVE="$1"

if [ ! -f "${ARCHIVE}" ]; then
  echo "Error: Archive file ${ARCHIVE} does not exist"
  exit 1
fi

echo "Inspecting archive contents..."
CONTENTS="$(tar -ztvf "${ARCHIVE}")"
echo "${CONTENTS}"

if [[ "${CONTENTS}" != *"etc/rollup"* ]]; then
  echo "Error: etc/rollup not found in archive"
  exit 1
fi

if [[ "${CONTENTS}" != *"usr/bin/away"* ]]; then
  echo "Error: usr/bin/away not found in archive"
  exit 1
fi

echo "rpm_archive verification passed successfully!"
