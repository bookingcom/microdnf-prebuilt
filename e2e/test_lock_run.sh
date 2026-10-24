#!/usr/bin/env bash
# Runs each rpm_lock `bazel run` script from the runfiles root (as `bazel run` does) and checks
# that the lockfile it writes into the workspace is byte-identical to the build action's output.
set -euo pipefail

RUNFILES_ROOT="${TEST_SRCDIR}/${TEST_WORKSPACE}"

# args: <name> <build-output lockfile> pairs
while [ "$#" -gt 1 ]; do
  name="$1"
  built="$2"
  shift 2
  runner="${RUNFILES_ROOT}/e2e/${name}_run.sh"
  [ -x "${runner}" ] || { echo "FAIL: missing runner ${runner}"; exit 1; }
  workspace="$(mktemp -d "${TEST_TMPDIR}/ws.XXXXXX")"
  echo "  ${name}"
  (cd "${RUNFILES_ROOT}" && BUILD_WORKSPACE_DIRECTORY="${workspace}" "${runner}")
  written="${workspace}/e2e/${name}.lock.json"
  [ -f "${written}" ] || { echo "FAIL: ${written} not written"; exit 1; }
  if ! cmp "${built}" "${written}"; then
    echo "FAIL: ${name}: run-script lockfile differs from build output"
    diff "${built}" "${written}" | head -20 || true
    exit 1
  fi
done
echo "rpm_lock run-script verification passed successfully!"
