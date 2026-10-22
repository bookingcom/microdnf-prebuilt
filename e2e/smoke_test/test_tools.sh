#!/usr/bin/env bash
set -euo pipefail

MICRODNF="$1"
RPM2ARCHIVE="$2"
DNF_LOCK="$3"
LOCK="$4"

echo "Testing downstream prebuilt microdnf..."
"${MICRODNF}" --help

echo "Testing downstream prebuilt rpm2archive..."
"${RPM2ARCHIVE}" --help

echo "Testing downstream prebuilt dnf_lock..."
"${DNF_LOCK}" --help

echo "Testing downstream prebuilt lock..."
"${LOCK}" --help

echo "Downstream tools verified successfully!"
