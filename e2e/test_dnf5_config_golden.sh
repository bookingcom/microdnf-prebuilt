#!/usr/bin/env bash
# Golden test: with the dnf_config preamble, dnf5's effective main and repository configuration holds no host path,
# and equals the checked-in golden file once the execroot and the scratch directory are replaced by placeholders.
set -euo pipefail

DNF5="$1"
CONF="$2"
REPO_DIR="$3"
GOLDEN="$4"

source "$(dirname "$0")/dnf5_lib.sh"

ROOT="$(pwd)"
OUT="$(mktemp -d)"
WORK="${OUT}/work"
actual="${OUT}/actual"
trap 'rm -rf "${OUT}"' EXIT
mkdir -p "${WORK}"
dnf5_preamble "${CONF}" "${REPO_DIR}" "${WORK}"

{
  "${DNF5}" "${PREAMBLE[@]}" --dump-main-config
  "${DNF5}" "${PREAMBLE[@]}" --dump-repo-config='*'
} >|"${OUT}/raw"
# Make the output path-stable: the scratch directory and the execroot become placeholders.
while IFS= read -r line; do
  line="${line//"${WORK}"/@TMP@}"
  echo "${line//"${ROOT}"/@ROOT@}"
done <"${OUT}/raw" >|"${actual}"

echo "=== no host path in any value ==="
# Paths are placeholders by now (/etc, /usr and /var are fine below the installroot, which is @TMP@/root), so a value
# that still starts with a slash, in a list element or in a URL or glob, is a host path.
host='(^|[ ,]|file://|glob:)(/|~|\$HOME)'
bad="$(grep -E ' = ' "${actual}" | sed -E 's/^[^=]*= //' | grep -E "${host}" || true)"
if [[ -n "${bad}" ]]; then
  echo "host paths in the configuration:" >&2
  echo "${bad}" >&2
  exit 1
fi
# $HOME as a path of its own, not as a substring: under remote execution HOME=/root, and @TMP@/root is no leak.
home_re="$(sed 's/[][\.*^$+?(){}|]/\\&/g' <<<"${HOME:-}")"
if [[ -n "${HOME:-}" && "${HOME}" != "/" ]] && grep -qE -- "(^|[ =,:])${home_re}(/|$)" "${actual}"; then
  echo "\$HOME leaked into the configuration" >&2
  exit 1
fi

echo "=== matches the golden file ==="
if ! diff -u "${GOLDEN}" "${actual}"; then
  echo "To accept the change, copy dnf5_config_golden.txt from the test's undeclared outputs over ${GOLDEN}." >&2
  cp "${actual}" "${TEST_UNDECLARED_OUTPUTS_DIR:-${OUT}}/dnf5_config_golden.txt"
  exit 1
fi
echo "dnf5 config golden test passed"
