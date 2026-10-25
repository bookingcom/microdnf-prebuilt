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

# Verify active RPM content is present
if [[ "${CONTENTS}" != *"etc/rollup"* ]]; then
  echo "Error: etc/rollup not found in archive"
  exit 1
fi

if [[ "${CONTENTS}" != *"usr/bin/away"* ]]; then
  echo "Error: usr/bin/away not found in archive"
  exit 1
fi

# Verify whiteouts generated from obsolete_rpms (mystery-devel contains usr/bin/my, usr/bin/ry, usr/bin/ste)
if [[ "${CONTENTS}" != *"usr/bin/.wh.my"* ]]; then
  echo "Error: usr/bin/.wh.my not found in archive"
  exit 1
fi

if [[ "${CONTENTS}" != *"usr/bin/.wh.ry"* ]]; then
  echo "Error: usr/bin/.wh.ry not found in archive"
  exit 1
fi

if [[ "${CONTENTS}" != *"usr/bin/.wh.ste"* ]]; then
  echo "Error: usr/bin/.wh.ste not found in archive"
  exit 1
fi

# Verify explicit whiteout
if [[ "${CONTENTS}" != *"etc/.wh.custom_deleted_config.conf"* ]]; then
  echo "Error: etc/.wh.custom_deleted_config.conf not found in archive"
  exit 1
fi

echo "rpm_archive whiteout verification passed successfully!"
