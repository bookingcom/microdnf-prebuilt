#!/usr/bin/env bash
set -euo pipefail

MICRODNF="$1"
RPM2ARCHIVE="$2"
DNF_LOCK="$3"

echo "Testing microdnf execution..."
"${MICRODNF}" --help

echo "Testing rpm2archive execution..."
"${RPM2ARCHIVE}" --help

echo "Testing dnf_lock execution..."
"${DNF_LOCK}" --help

echo "All tools executed successfully!"
