#!/usr/bin/env bash
set -euo pipefail

MICRODNF="$1"
RPM2ARCHIVE="$2"
RPM2FILES="$3"
DNF5="$4"

echo "Testing microdnf execution..."
"${MICRODNF}" --help

echo "Testing rpm2archive execution..."
"${RPM2ARCHIVE}" --help

echo "Testing rpm2files execution..."
"${RPM2FILES}" --help

echo "Testing dnf5 execution..."
"${DNF5}" --help

echo "Testing dnf5 lock and rpmdb-mark execution..."
"${DNF5}" lock --help
"${DNF5}" rpmdb-mark --help

echo "All tools executed successfully!"
