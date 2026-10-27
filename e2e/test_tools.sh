#!/usr/bin/env bash
set -euo pipefail

MICRODNF="$1"
RPM2ARCHIVE="$2"
DNF_LOCK="$3"
RPM2FILES="$4"
MARK="$5"

echo "Testing microdnf execution..."
"${MICRODNF}" --help

echo "Testing rpm2archive execution..."
"${RPM2ARCHIVE}" --help

echo "Testing dnf_lock execution..."
"${DNF_LOCK}" --help

echo "Testing rpm2files execution..."
"${RPM2FILES}" --help

echo "Testing mark execution..."
"${MARK}" --help

echo "All tools executed successfully!"
