#!/usr/bin/env bash
set -euo pipefail

RPM_FILE="$1"

if [ ! -f "${RPM_FILE}" ]; then
  echo "Error: RPM file ${RPM_FILE} does not exist"
  exit 1
fi

echo "Verifying RPM file size..."
SIZE=$(wc -c < "${RPM_FILE}")
if [ "${SIZE}" -lt 100 ]; then
  echo "Error: RPM file is unexpectedly small: ${SIZE} bytes"
  exit 1
fi

echo "RPM repository verification passed successfully!"
