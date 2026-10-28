# microdnf-prebuilt

Bazel repository capable of building `microdnf`, `rpm2archive`, and `dnf_lock` hermetically across platforms.

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

#### dnf options
| Option | Behaviour |
| --- | --- |
| `--excludepkgs=<glob,…>` / `--setopt=excludepkgs=…` | dnf `excludepkgs`: matching packages are invisible to the solver (a cut-off like `ca-certificates-2026*`). Repo-scoped `--setopt=<repo>.excludepkgs=…` also works. Unlike `--exclude`, which prunes resolved packages out of the lock graph. |
| `--module-enable=<name:stream>` | Enables a module stream before resolving, like `dnf module enable`. The platform comes from `/etc/os-release`'s `PLATFORM_ID` or `--setopt=module_platform_id=platform:el9`. |
| `--best` / `--nobest` | **Best is the default for the whole transaction**, as in dnf with EL's `best=True`: every requested package must resolve to its newest candidate and dependencies may not be silently held back on an older version. To opt out, pass `--nobest` (or `--setopt=best=False`, or `nobest = True` on `rpm_lock`); the solver may then fall back to older candidates. `--nobest` keeps the constraints of the request: `foo-2.0-1.noarch --nobest` never falls back to `foo-1.0`. `--best` together with `--nobest` is an error. |
| `--forcearch=<arch>` | Solver architecture. It defaults to the first non-`noarch`/`src` `--archlist` entry, so an x86_64 lock resolves on an arm64 host. The same architecture feeds `$basearch` in repo URLs (`i686` maps to `i386`, `armv7hl` to `armhfp`); an explicit `DNF_VAR_basearch` in the environment is overridden. |

When several enabled repos carry the same package (the same NEVRA in appstream and baseos, say), the lock holds it once, taken from the repo that wins on priority and cost.

The lock holds every package the solver installs, weak dependencies (Recommends, matching Supplements) included, as dnf does with its default `install_weak_deps=True`; `--setopt=install_weak_deps=False` leaves them out. `--exclude` is the one thing that prunes the result: it drops the excluded packages and whatever is reachable only through them (following Recommends and Supplements too).

Any other `--setopt=<key>=<value>` or `<repo>.<key>=<value>` goes to libdnf. `rpm_lock` exposes the same knobs as `excludepkgs`,
`forcearch`, `module_enable`, `module_platform_id`, `nobest` and `install_weak_deps` attributes.

### 3. Hermetic Host Portability
- Automatic hermetic fallback configuration when `/usr/lib/rpm/rpmrc` is absent on host systems.
- Unprivileged user fallback redirecting caches to `~/.cache/microdnf` and repos to a fallback directory when `/etc/yum.repos.d` is missing.

## Building

```bash
# Build standalone binaries for the host
bazel build //:microdnf //:rpm2archive //:dnf_lock

# Build cross-compiled binaries for all platforms (linux_amd64, linux_arm64, osx_amd64, osx_arm64)
bazel build //:microdnf_all //:rpm2archive_all //:dnf_lock_all
```

## License

This project is licensed under the [Apache License, Version 2.0](LICENSE).

The Apache 2.0 license is a permissive open source license that allows free use, modification, reproduction, and distribution in both open source and private closed-source / commercial software projects.
