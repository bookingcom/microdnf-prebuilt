#!/usr/bin/env bash
# Usage: normalize_lock.sh <in> <out>
# The lockfile url of a repo-local package is the absolute path of the action that wrote it, which differs per
# action and per sandbox implementation (darwin-sandbox/<N>, bazel-sandbox.<hash>/linux-sandbox/<N>, or the
# output base without a sandbox; slashes JSON-escaped). Replace everything up to execroot/ so that the lockfiles
# of two separate actions can be compared byte for byte.
set -euo pipefail
sed -E 's#"file:[^"]*(\\?/)execroot(\\?/)#"file:@EXECROOT@\2#g' "$1" >"$2"
