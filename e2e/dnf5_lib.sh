#!/usr/bin/env bash
# Shared by the dnf5 tests: expands the dnf_config preamble (one argument per line, path tokens unexpanded).
#   dnf5_preamble <dnf.conf of the dnf_config target> <repository directory> <scratch directory>
# fills the PREAMBLE array. Paths are relative to the current directory (the runfiles root) or absolute.

dnf5_preamble() {
  local conf="$1" confdir repodir line
  confdir="$(cd "$(dirname "${conf}")" && pwd)"
  repodir="$(cd "$2" && pwd)"
  PREAMBLE=()
  while IFS= read -r line; do
    line="${line//@CONFDIR@/${confdir}}"
    line="${line//@REPODIR@/${repodir}}"
    line="${line//@TMP@/$3}"
    PREAMBLE+=("${line}")
  done <"${confdir}/preamble.args"
}
