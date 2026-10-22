#!/usr/bin/env bash
set -euo pipefail

AWAY_BIN="$1"
ROLLUP_FILE="$2"

if [ ! -x "${AWAY_BIN}" ]; then
  echo "Error: ${AWAY_BIN} is not executable"
  exit 1
fi

OUT="$(cat "${AWAY_BIN}")"
echo "Binary content: ${OUT}"
if [[ "${OUT}" != *"take you away"* ]]; then
  echo "Error: Content did not match expected 'take you away'"
  exit 1
fi

if [ ! -f "${ROLLUP_FILE}" ]; then
  echo "Error: ${ROLLUP_FILE} does not exist"
  exit 1
fi

echo "rpm_extract verification passed successfully!"
