#!/usr/bin/env bash
set -euo pipefail

WAPP="$1"
TRANSACTION="$2"
SATISFIED="$3"
ARCHIVE="$4"
ZSTD_ARCHIVE="$5"

fail() { echo "FAIL: $1"; exit 1; }

# has FILE LINE: exact line match
has() { while IFS= read -r l; do [[ "${l}" == "$2" ]] && return 0; done < "$1"; return 1; }

echo "--- wapp"; cat "${WAPP}"
has "${WAPP}" "rpm wapp-1-1.noarch.rpm" || fail "wapp lacks its own rpm"
has "${WAPP}" "rpm wrec-1-1.noarch.rpm" || fail "weak dependency rpm missing from wapp"
! has "${WAPP}" "package hostroot" || fail "wapp must not reach the installed-root package"

echo "--- transaction"; cat "${TRANSACTION}"
for p in wapp wrec hostroot; do
  has "${TRANSACTION}" "package ${p}" || fail "transaction lacks ${p}"
  has "${TRANSACTION}" "rpm ${p}-1-1.noarch.rpm" || fail "transaction lacks ${p} rpm"
done

echo "--- satisfied"; cat "${SATISFIED}"
while IFS= read -r l; do
  [[ "${l}" == rpm\ * || "${l}" == package\ * ]] && fail "satisfied request must carry no rpms: ${l}"
done < "${SATISFIED}"

echo "--- empty archive"
[ -s "${ARCHIVE}" ] || fail "empty archive not written"
COUNT="$(tar -ztf "${ARCHIVE}" | wc -l | tr -d ' ')"
[ "${COUNT}" = "0" ] || fail "empty archive lists ${COUNT} entries"

echo "--- empty zstd archive"
[ -s "${ZSTD_ARCHIVE}" ] || fail "empty zstd archive not written"
MAGIC="$(head -c 4 "${ZSTD_ARCHIVE}" | od -An -tx1 | tr -d ' \n')"
[ "${MAGIC}" = "28b52ffd" ] || fail "zstd magic expected, got ${MAGIC}"
# Entry count only when python can decompress zstd; no host zstd dependency.
python3 - "${ZSTD_ARCHIVE}" <<'PY' || fail "empty zstd archive lists entries"
import sys, tarfile, io
data = open(sys.argv[1], "rb").read()
try:
    from compression import zstd as z
    raw = z.decompress(data)
except ImportError:
    try:
        import zstandard
        raw = zstandard.ZstdDecompressor().decompressobj().decompress(data)
    except ImportError:
        print("no python zstd module; magic check only")
        sys.exit(0)
assert not tarfile.open(fileobj=io.BytesIO(raw)).getnames()
PY

echo "transaction graph verification passed successfully!"
