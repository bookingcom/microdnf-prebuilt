# Agent Guidelines & Repository Learnings

This document captures critical architectural patterns, protocols, and operational learnings for AI agents and maintainers working on `microdnf-prebuilt`.

---

## 1. Tag Immutability & Release Lifecycle

> [!IMPORTANT]
> **Git tags in this repository are strictly immutable.**
> Once a tag is created and pushed, it must **NEVER** be deleted, overwritten, or force-pushed.

### Why Tags Must Be Immutable
- **Downstream Cache Integrity**: Downstream Bazel consumers (via Bzlmod, Bazel Central Registry / BCR, and `http_file` / `http_archive`) cache downloads by URL and SRI integrity hash (`sha256-...`). Mutating or re-creating a tag causes checksum mismatches, build failures, and cache poisoning.
- **Reproducible Builds**: A release tag corresponds to a distinct set of binary artifacts and a release tarball. Immutability ensures bit-for-bit reproducibility over time.

### Versioning & Tagging Scheme
Tags follow the BCR-compatible naming format:
```
microdnf-prebuilt-v<upstream_short_sha>.bcr.<build_id>
```
Example: `microdnf-prebuilt-vc6a9aa9.bcr.1`

- **`<upstream_short_sha>`**: The 7-character hexadecimal prefix of the upstream [`rpm-software-management/microdnf`](https://github.com/rpm-software-management/microdnf) git commit.
- **`<build_id>`**: An integer counter starting at `1`.
- **Handling Fixes/Re-releases**:
  If a release fails, requires packaging fixes, or needs regenerated binaries for the same upstream commit:
  - **Do NOT delete or move the existing tag.**
  - **Increment the build ID**: `bcr.1` -> `bcr.2` -> `bcr.3`.
  - Update any internal references and push the new tag.

---

## 2. Release Packaging & Tarball Structure

The release source tarball (`microdnf-prebuilt-<tag>.tar.gz`) is a standalone, lightweight Bzlmod module consumed downstream.

### Inclusion Rules
- **Include**:
  - `microdnf/` directory (Public API):
    - `BUILD.bazel`: Defines toolchain rules and host selection.
    - `extensions.bzl`: Public Bzlmod extension entrypoint.
    - `extension.bzl`: Public alias for `extensions.bzl`.
  - `microdnf/private/` directory (Internal implementation):
    - `BUILD.bazel`: Private package boundary.
    - `defs.bzl`: Internal definitions.
    - `toolchain.bzl`: Implements `microdnf_toolchain` rule and `MicrodnfInfo` provider.
    - `extensions.bzl`: Implements `microdnf_prebuilt_extension` and host alias repository rules.
    - `integrities.bzl`: Dynamically generated file containing SRI hashes and URLs for prebuilt binaries.
    - `platforms.bzl`: Supported target platforms definitions.
  - Root `BUILD.bazel`: Minimal public visibility rule (`package(default_visibility = ["//visibility:public"])`).
  - Root `MODULE.bazel`: Stripped module file.

### Exclusion Rules
- **Exclude**:
  - `bazel/`: Build-time platforms, registry, upstream tracking (`bazel/extensions.bzl`, `bazel/microdnf.BUILD.bazel`, `bazel/BUILD.bazel`, `bazel/gresource.bzl`).
  - `dnf/`: Upstream and local C source code, headers, and plugin implementations.
  - `bazel/3rdparty/`: Compilation dependencies registry.
  - All C/C++ compilation leaf dependencies in `MODULE.bazel` (e.g., `libdnf`, `glib`, `rpm`, `llvm`, `libsolv`, `librepo`, `libmodulemd`, `json-c`, `uthash`, etc.). Downstream consumers only require prebuilt binary toolchains, not the compiler dependencies.

---

## 3. Prebuilt Binaries & Host Alias Repositories

### Supported Platforms
Cross-compilation is supported for 4 platforms:
- `linux_amd64`
- `linux_arm64`
- `osx_amd64`
- `osx_arm64`

### Tools Built
1. **`microdnf`**: Standalone multi-command package manager.
2. **`rpm2archive`**: Standalone tool converting RPM package lists into deduplicated container layers.
3. **`dnf_lock`**: Standalone dependency solver emitting JSON lockfiles.

### Tool Naming Protocol
- The dependency solver is named **`dnf_lock`** across the entire repository (`//:dnf_lock`, `//dnf:dnf_lock`, `@dnf_lock//:dnf_lock`).
- **No compatibility aliases**: Do NOT introduce aliases for `lock`. Using `dnf_lock` avoids naming collisions with generic system lock utilities and accurately identifies the tool's purpose.

### Host Alias Repository Pattern
Modeled after `jq_host_alias_repo` in `bazel-contrib/jq.bzl`:
- Downstream consumers access host-executable tools directly via:
  - `@microdnf//:microdnf`
  - `@rpm2archive//:rpm2archive`
  - `@dnf_lock//:dnf_lock`
- Each repository rule (`microdnf_host_alias_repo`, `rpm2archive_host_alias_repo`, `dnf_lock_host_alias_repo`) inspects `rctx.os.name` and `rctx.os.arch` to alias directly to the matching `@<tool>_<platform>//file`.

### Upstream Repository Naming (`microdnf_src`)
- In `bazel/extensions.bzl`, the upstream source git repository is named **`microdnf_src`** (not `microdnf`).
- This prevents a Bzlmod repository name collision in `MODULE.bazel` with the `@microdnf` host alias repository.

---

## 4. Bazel Invocation Protocol

All Bazel commands (`build`, `test`, `run`, `cquery`, `query`, etc.) must be invoked with an explicitly generated UUID:

```bash
INVOCATION_ID=$(uuidgen) && echo "Invocation ID: ${INVOCATION_ID}" && bazel <command> --invocation_id=${INVOCATION_ID} <args>
```

### Remote Execution, Cache & Output Download Modes
- **Zero Disk Cache**: Local runner disk caching is disabled entirely across CI and Release workflows (`disk-cache: false` in `setup-bazel`). This ensures all builds query and populate BuildBuddy remote cache consistently, preventing cache warming divergence between workflows.
- **Remote Cache Chunking**: Enabled `--experimental_remote_cache_chunking` in `.bazelrc` and CI configuration to chunk large blobs via FastCDC, optimizing remote CAS storage and transfer efficiency.
- **Build Without the Bytes (`--remote_download_outputs=minimal`)**:
  - The default RBE and CI configuration (`build:rbe`, `build:ci`) enforces `--remote_download_outputs=minimal`.
  - In CI, builds and tests run completely without downloading remote artifacts.
- **Downloading Needed Binaries for Packaging**:
  - In release workflows or when running packaging scripts (`package_release.sh`), only the final top-level targets are materialized:
    ```bash
    bazel build //:microdnf_all //:rpm2archive_all //:dnf_lock_all --config=ci --remote_download_outputs=toplevel
    ```
    This ensures only the 12 needed release binaries are downloaded to the runner while intermediate build artifacts stay in the remote CAS.

---

## 5. Renovate & RE2 Regular Expressions

The Renovate configuration (`renovate.json`) is validated with strict RE2 syntax:
- **No Backreferences**: The Google RE2 engine strictly prohibits backreferences (`\1`, `\k<name>`).
- **Safe Matching Patterns**: Use generic character classes (e.g., `[^/]+`) rather than backreferences when matching package names or repository paths.

---

## 6. Timezone & Communication Guidelines

- **Timezone**: All timestamps, dates, and build times presented in reports, summaries, or commit notes must be localized to the Amsterdam timezone (`Europe/Amsterdam`, CET/CEST, UTC+1 / UTC+2).
- **File Links**: Always link to files and code symbols using GitHub-flavored markdown links (`[target](file:///path/to/target)`).
- **Conciseness**: Keep updates focused, structured, and informative.
