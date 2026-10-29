#!/usr/bin/env bash
set -euo pipefail

TAG="${1:-${GITHUB_REF_NAME:-}}"
FULL_SHA="${2:-}"
SHORT_SHA="${3:-}"
BUILD_ID="${4:-}"

if [ -z "${TAG}" ]; then
  echo "Error: Tag name must be provided as first argument or GITHUB_REF_NAME" >&2
  exit 1
fi

if [[ "${TAG}" =~ microdnf-prebuilt-v([0-9a-fA-F]+)\.bcr\.([0-9]+) ]]; then
  SHORT_SHA="${SHORT_SHA:-${BASH_REMATCH[1]}}"
  BUILD_ID="${BUILD_ID:-${BASH_REMATCH[2]}}"
  RELEASE_VERSION="${SHORT_SHA}.bcr.${BUILD_ID}"
elif [[ "${TAG}" =~ v([0-9a-fA-F]+)\.bcr\.([0-9]+) ]]; then
  SHORT_SHA="${SHORT_SHA:-${BASH_REMATCH[1]}}"
  BUILD_ID="${BUILD_ID:-${BASH_REMATCH[2]}}"
  RELEASE_VERSION="${SHORT_SHA}.bcr.${BUILD_ID}"
else
  RELEASE_VERSION="${TAG#v}"
fi

echo "Packaging release for tag: ${TAG} (version: ${RELEASE_VERSION})"

mkdir -p release_assets

BASE_URL="https://github.com/${GITHUB_REPOSITORY:-bookingcom/microdnf-prebuilt}/releases/download/${TAG}"

# 1. Collect all 16 prebuilt binaries and compute integrity hashes
declare -A INTEGRITIES
# The file in the output directory of //dnf:<tool>_<platform>. platform_transition_binary keeps the name of the binary
# it wraps, which is the tool's unless listed here.
declare -A SRC_NAMES=([dnf5]=dnf5_binary)

for target in dnf5 microdnf rpm2archive rpm2files; do
  for platform in linux_amd64 linux_arm64 osx_amd64 osx_arm64; do
    bin_name="${target}_${platform}"
    src_file="bazel-bin/dnf/${bin_name}/${SRC_NAMES[${target}]:-${target}}"
    dest_file="release_assets/${bin_name}"

    if [ ! -f "${src_file}" ]; then
      echo "Error: Required binary not found: ${src_file}" >&2
      exit 1
    fi

    cp -L "${src_file}" "${dest_file}"
    chmod +x "${dest_file}"

    # Calculate Subresource Integrity (SRI) sha256-<base64>
    sri_hash="sha256-$(openssl dgst -sha256 -binary "${dest_file}" | base64)"
    INTEGRITIES["${bin_name}"]="${sri_hash}"
    echo "  ${bin_name} -> ${sri_hash}"
  done
done

# 2. Generate microdnf/private/integrities.bzl with computed SRI hashes
cat > microdnf/private/integrities.bzl << EOF
"""Integrity checksums and URLs for prebuilt microdnf binaries."""

RELEASE_TAG = "${TAG}"
BASE_URL = "${BASE_URL}"

INTEGRITIES = {
EOF

for name in $(echo "${!INTEGRITIES[@]}" | tr ' ' '\n' | sort); do
  echo "    \"${name}\": \"${INTEGRITIES[${name}]}\"," >> microdnf/private/integrities.bzl
done

echo "}" >> microdnf/private/integrities.bzl

echo "Generated microdnf/private/integrities.bzl successfully."

# 3. Assemble clean release package directory
STAGING_DIR="$(mktemp -d)"
trap 'rm -rf "${STAGING_DIR}"' EXIT

PREFIX="microdnf-prebuilt-${TAG#microdnf-prebuilt-}"
PKG_DIR="${STAGING_DIR}/${PREFIX}"

mkdir -p "${PKG_DIR}/microdnf/private"

# Minimal root BUILD.bazel
cat > "${PKG_DIR}/BUILD.bazel" << 'EOF'
package(default_visibility = ["//visibility:public"])
EOF

# Stripped MODULE.bazel without compilation leaf dependencies
cat > "${PKG_DIR}/MODULE.bazel" << EOF
module(
    name = "microdnf_prebuilt",
    version = "${RELEASE_VERSION}",
)

bazel_dep(name = "platforms", version = "1.1.0")

# Prebuilt binary repositories and host alias repositories
microdnf_prebuilt = use_extension("//microdnf:extensions.bzl", "microdnf_prebuilt_extension")
use_repo(
    microdnf_prebuilt,
    "dnf5",
    "dnf5_linux_amd64",
    "dnf5_linux_arm64",
    "dnf5_osx_amd64",
    "dnf5_osx_arm64",
    "microdnf",
    "microdnf_linux_amd64",
    "microdnf_linux_arm64",
    "microdnf_osx_amd64",
    "microdnf_osx_arm64",
    "rpm2archive",
    "rpm2archive_linux_amd64",
    "rpm2archive_linux_arm64",
    "rpm2archive_osx_amd64",
    "rpm2archive_osx_arm64",
    "rpm2files",
    "rpm2files_linux_amd64",
    "rpm2files_linux_arm64",
    "rpm2files_osx_amd64",
    "rpm2files_osx_arm64",
)

# Register prebuilt microdnf toolchains
register_toolchains(
    "//microdnf:toolchain_linux_amd64",
    "//microdnf:toolchain_linux_arm64",
    "//microdnf:toolchain_osx_amd64",
    "//microdnf:toolchain_osx_arm64",
    "//microdnf:toolchain_host",
)
EOF

# Copy microdnf public files (*.bzl and BUILD.bazel)
cp microdnf/*.bzl "${PKG_DIR}/microdnf/"
cp microdnf/BUILD.bazel "${PKG_DIR}/microdnf/BUILD.bazel"

# Copy microdnf private files (*.bzl and BUILD.bazel)
cp microdnf/private/*.bzl "${PKG_DIR}/microdnf/private/"
cp microdnf/private/BUILD.bazel "${PKG_DIR}/microdnf/private/BUILD.bazel"

# In packaged microdnf/BUILD.bazel, bind the rpm2files and dnf5 aliases to prebuilt binary select
python3 -c '
path = "'"${PKG_DIR}"'/microdnf/BUILD.bazel"
with open(path, "r") as f:
    content = f.read()
for tool, source in (("rpm2files", "//rpm2files:rpm2files"), ("dnf5", "//:dnf5")):
    target = """alias(
    name = "%s",
    actual = "%s",
    visibility = ["//visibility:public"],
)""" % (tool, source)
    replacement = """alias(
    name = "%s",
    actual = select({
        ":is_" + platform: "@%s_" + platform + "//file"
        for platform in SUPPORTED_PLATFORMS
    }),
    visibility = ["//visibility:public"],
)""" % (tool, tool)
    if target not in content:
        raise SystemExit("microdnf/BUILD.bazel has no %s alias to bind" % tool)
    content = content.replace(target, replacement)
with open(path, "w") as f:
    f.write(content)
'

# 4. Create release tarball archive
ARCHIVE_NAME="${TAG}.tar.gz"
ARCHIVE_PATH="release_assets/${ARCHIVE_NAME}"

tar -czf "${ARCHIVE_PATH}" -C "${STAGING_DIR}" "${PREFIX}"
ARCHIVE_SHA256=$(sha256sum "${ARCHIVE_PATH}" | awk '{print $1}')

echo "Created release archive: ${ARCHIVE_PATH} (sha256: ${ARCHIVE_SHA256})"

# 5. Generate release notes
DISPLAY_TAG="${TAG#microdnf-prebuilt-}"

cat > release_notes.md << EOF
## microdnf-prebuilt ${DISPLAY_TAG}

### Upstream Provenance
- **Upstream Project**: [rpm-software-management/microdnf](https://github.com/rpm-software-management/microdnf)
- **Upstream Commit**: [\`${FULL_SHA}\`](https://github.com/rpm-software-management/microdnf/commit/${FULL_SHA}) (\`${SHORT_SHA}\`)
- **Build ID**: \`${BUILD_ID}\`

### Artifacts & Integrity
- **Release Source Archive**: \`${ARCHIVE_NAME}\`
- **SHA256**: \`${ARCHIVE_SHA256}\`

### Bzlmod Usage

\`\`\`starlark
bazel_dep(name = "microdnf_prebuilt", version = "${RELEASE_VERSION}")
\`\`\`

### Prebuilt Binaries & Integrities
Prebuilt standalone executables and toolchains are available for:
- \`linux_amd64\` (\`microdnf\`, \`rpm2archive\`, \`rpm2files\`, \`dnf5\`)
- \`linux_arm64\` (\`microdnf\`, \`rpm2archive\`, \`rpm2files\`, \`dnf5\`)
- \`osx_amd64\` (\`microdnf\`, \`rpm2archive\`, \`rpm2files\`, \`dnf5\`)
- \`osx_arm64\` (\`microdnf\`, \`rpm2archive\`, \`rpm2files\`, \`dnf5\`)
EOF

if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "archive_path=${ARCHIVE_PATH}" >> "${GITHUB_OUTPUT}"
  echo "archive_sha256=${ARCHIVE_SHA256}" >> "${GITHUB_OUTPUT}"
fi

echo "Package release completed successfully."
