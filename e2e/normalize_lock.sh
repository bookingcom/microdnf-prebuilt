#!/usr/bin/env bash
# Usage: normalize_lock.sh <in> <out>
# The lockfile url of a repo-local package is the absolute path of the action that wrote it, which differs per
# action and per sandbox implementation (darwin-sandbox/<N>, bazel-sandbox.<hash>/linux-sandbox/<N>, or the
# output base without a sandbox; slashes JSON-escaped). Replace everything up to execroot/ so that the lockfiles
# of two separate actions can be compared byte for byte. A remote executor runs in an execroot of its own name
# (/buildbuddy-execroot/bazel-out/...), which becomes the same @EXECROOT@/_main. The output configuration directory
# (darwin_arm64-fastbuild, k8-fastbuild-ST-<hash>, ...) differs per host and flags, so it is replaced too: the result
# also equals the golden files.
set -euo pipefail
sed -E -e 's#"file:[^"]*(\\?/)execroot(\\?/)#"file:@EXECROOT@\2#g' \
  -e 's#"file:(\\?/)\\?/\\?/[^"@]*bazel-out\\?/#"file:@EXECROOT@\1_main\1bazel-out\1#g' \
  -e 's#bazel-out(\\?/)[^\\/"]+(\\?/)bin#bazel-out\1@CONFIG@\2bin#g' "$1" >"$2"
