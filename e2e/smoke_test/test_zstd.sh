#!/usr/bin/env bash
set -euo pipefail

# Args: <zstd archive> <gzip away> <gzip rollup> <zstd away> <zstd rollup>
ARCHIVE="$1"
GZ_AWAY="$2"
GZ_ROLLUP="$3"
ZST_AWAY="$4"
ZST_ROLLUP="$5"

MAGIC="$(head -c 4 "${ARCHIVE}" | od -An -tx1 | tr -d ' \n')"
if [ "${MAGIC}" != "28b52ffd" ]; then
  echo "Error: ${ARCHIVE} does not start with zstd magic 28b52ffd (got ${MAGIC})"
  exit 1
fi

if [ ! -x "${ZST_AWAY}" ]; then
  echo "Error: ${ZST_AWAY} is not executable"
  exit 1
fi

cmp "${GZ_AWAY}" "${ZST_AWAY}"
cmp "${GZ_ROLLUP}" "${ZST_ROLLUP}"

echo "zstd archive and rpm_extract verification passed successfully!"
