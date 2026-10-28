# microdnf-prebuilt

Bazel repository capable of building `microdnf`, `rpm2archive`, `dnf_lock`, `rpm2files`, and `dnf5` hermetically across platforms.

## Architecture

- **Upstream Tracking**: Tracks upstream `microdnf` as a Bzlmod module extension (`//bazel:extensions.bzl%microdnf_extension`) using `git_repository` pinned to upstream commit [`c6a9aa90902b5f050825003398d3a8fd4542e533`](https://github.com/rpm-software-management/microdnf/commit/c6a9aa90902b5f050825003398d3a8fd4542e533).
- **Hermetic Toolchains**: Hermetic Clang/LLVM, Python 3.14 via `aspect_rules_py`, and Remote Execution (RBE) via `toolchains_buildbuddy`.
- **Multi-Platform Cross-Compilation**: Supports compilation and cross-compilation for `linux_amd64`, `linux_arm64`, `osx_amd64`, and `osx_arm64`.

## Features

### 1. `rpm2archive`
Generates a single deduplicated layer archive (`.tar`, `.tar.gz`, `.tar.zst`) from a list of RPM packages for container layers (e.g. `rules_oci` / `rules_image`).
- Deduplicates file conflicts across RPMs where packages later in the command-line argument list take precedence.
- Emits parent directory entries exactly once without duplicate records.
- Available as a standalone CLI (`//:rpm2archive`) and as a `microdnf` subcommand plugin (`microdnf rpm2archive`).

```bash
# Standalone CLI
bazel run //:rpm2archive -- -o /path/to/layer.tar.zst pkg1.rpm pkg2.rpm

# Subcommand plugin
bazel run //:microdnf -- rpm2archive -o /path/to/layer.tar.gz pkg1.rpm pkg2.rpm
```

### 2. `dnf_lock`
Resolves package dependency closures and emits formatted JSON lockfiles for reproducible builds and module extensions.
- Solves complete dependency closures via `libdnf`'s solver without requiring local root privileges.
- Outputs package details: `name`, `version`, `arch`, `nevra`, remote `url`, SRI `integrity` (`sha256-...`), hex `checksum`, and `dependencies`.
- Pre-parses repository configuration flags (`--setopt=reposdir=...`, `--setopt=cachedir=...`, `--config=...`, `--releasever=...`, `--enablerepo=...`) prior to context setup.
- Available as a standalone CLI (`//:dnf_lock`) and as a `microdnf` subcommand plugin (`microdnf lock`).

```bash
# Standalone CLI
bazel run //:dnf_lock -- -o lockfile.json --setopt=reposdir=/path/to/repos.d ed bash

# Subcommand plugin
bazel run //:microdnf -- lock -o lockfile.json --setopt=reposdir=/path/to/repos.d ed bash
```

### 3. Hermetic Host Portability
- Automatic hermetic fallback configuration when `/usr/lib/rpm/rpmrc` is absent on host systems.
- Unprivileged user fallback redirecting caches to `~/.cache/microdnf` and repos to a fallback directory when `/etc/yum.repos.d` is missing.

## Building

```bash
# Build standalone binaries for the host
bazel build //:microdnf //:rpm2archive //:dnf_lock //:rpm2files //:dnf5

# Build cross-compiled binaries for all platforms (linux_amd64, linux_arm64, osx_amd64, osx_arm64)
bazel build //:microdnf_all //:rpm2archive_all //:dnf_lock_all //:rpm2files_all //:dnf5_all
```

## License

This project is licensed under the [Apache License, Version 2.0](LICENSE).

The Apache 2.0 license is a permissive open source license that allows free use, modification, reproduction, and distribution in both open source and private closed-source / commercial software projects.
