#!/usr/bin/env bash
set -euo pipefail

AWAY_BIN="$1"

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

echo "Smoke test passed successfully!"
