# microdnf-prebuilt

Bazel repository capable of building `microdnf`, `rpm2archive`, `rpm2files`, and `dnf5` hermetically across platforms.

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

### 2. `dnf5 lock`
Resolves package dependency closures and emits formatted JSON lockfiles for reproducible builds and module extensions.
- Solves complete dependency closures via `libdnf5`'s solver without requiring local root privileges.
- Outputs package details: `name`, `version`, `arch`, `nevra`, remote `url`, SRI `integrity` (`sha256-...`), hex `checksum`, `dependencies`, and, when the package has any, its `recommends` and `supplements` (dependency strings). A later `--lockfile` run reads them as what the layer's build left unmet, so an upgrade adds only the weak dependencies that are new even when the repositories no longer hold that build; entries without them (older lockfiles) lock all weak dependencies of the new build.
- Takes the repository configuration from the usual dnf5 options (`--setopt=reposdir=...`, `--config=...`, `--releasever=...`, `--enablerepo=...`).
- Runs as the `lock` command of `dnf5` (`//:dnf5`); the `dnf_lock` tool and the `microdnf lock` plugin were removed.

```bash
bazel run //:dnf5 -- lock -o lockfile.json --setopt=reposdir=/path/to/repos.d ed bash
```

#### dnf options
| Option | Behaviour |
| --- | --- |
| `--excludepkgs=<glob,…>` / `--setopt=excludepkgs=…` | dnf `excludepkgs`: matching packages are invisible to the solver (a cut-off like `ca-certificates-2026*`). Repo-scoped `--setopt=<repo>.excludepkgs=…` also works. Unlike `--prune`, which prunes resolved packages out of the lock graph. |
| `--module-enable=<name:stream>` | Enables a module stream before resolving, like `dnf module enable`. The platform comes from `/etc/os-release`'s `PLATFORM_ID` or `--setopt=module_platform_id=platform:el9`. |
| `--best` / `--nobest` | **Best is the default for the whole transaction**, as in dnf with EL's `best=True`: every requested package must resolve to its newest candidate and dependencies may not be silently held back on an older version. To opt out, pass `--nobest` (or `--setopt=best=False`, or `nobest = True` on `rpm_lock`); the solver may then fall back to older candidates. `--nobest` keeps the constraints of the request: `foo-2.0-1.noarch --nobest` never falls back to `foo-1.0`. `--best` together with `--nobest` is an error. |
| `--forcearch=<arch>` | Solver architecture. It defaults to the first non-`noarch`/`src` `--archlist` entry, so an x86_64 lock resolves on an arm64 host. The same architecture feeds `$basearch` in repo URLs (`i686` maps to `i386`, `armv7hl` to `armhfp`); an explicit `DNF_VAR_basearch` in the environment is overridden. |

When several enabled repos carry the same package (the same NEVRA in appstream and baseos, say), the lock holds it once, taken from the repo that wins on priority and cost.

The lock holds every package the solver installs, weak dependencies (Recommends, matching Supplements) included, as dnf does with its default `install_weak_deps=True`; `--setopt=install_weak_deps=False` leaves them out. `--prune` is the one thing that prunes the result: it drops the excluded packages and whatever is reachable only through them (following Recommends and Supplements too).

Any other `--setopt=<key>=<value>` or `<repo>.<key>=<value>` goes to libdnf5. `rpm_lock` exposes the same knobs as `excludepkgs`,
`forcearch`, `module_enable`, `module_platform_id`, `nobest` and `install_weak_deps` attributes.

#### `rpm_lock`
The `rpm_lock` rule solves with `dnf5 lock` under a hermetic [`dnf_config`](microdnf/private/config.bzl). The tool is the
`dnf5_tool` attribute, default `//microdnf:dnf5` (the prebuilt `dnf5` of the host platform in a release). The `backend`,
`lock_tool` and `mark_tool` attributes of the former libdnf path were removed: delete `backend = ...` from your targets
(the `dnf5` backend was already the default), and use `dnf5_tool` instead of `lock_tool`/`mark_tool`.

```starlark
rpm_lock(
    name = "base_lock",
    arch = ["x86_64", "noarch"],
    packages = ["bash"],
    releasever = "9",
    repo_config = "ol9.repo",
)
```

`exclude` becomes `dnf5 lock --prune`. The lockfiles of the `rpm_lock` cases in `//e2e` are checked in under
[`e2e/testdata/lock_golden`](e2e/testdata/lock_golden) (normalized by `//e2e:normalize_lock`) and diffed by
`//e2e/testdata:lock_golden_tests`; regenerate them with `bazel run //e2e/testdata:lock_golden`.

`rpm_mark` records the packages with `dnf5 rpmdb-mark` (`dnf5_tool`) into `var/lib/rpm/rpmdb.sqlite`;
`//e2e:oracle_linux_curl_mark_test` asserts the exact package set.

### 3. Hermetic Host Portability
- Automatic hermetic fallback configuration when `/usr/lib/rpm/rpmrc` is absent on host systems.
- Unprivileged user fallback redirecting caches to `~/.cache/microdnf` and repos to a fallback directory when `/etc/yum.repos.d` is missing.

## Building

```bash
# Build standalone binaries for the host
bazel build //:microdnf //:rpm2archive //:rpm2files //:dnf5

# Build cross-compiled binaries for all platforms (linux_amd64, linux_arm64, osx_amd64, osx_arm64)
bazel build //:microdnf_all //:rpm2archive_all //:rpm2files_all //:dnf5_all
```

## Sanitizers

`--config=asan` builds `dnf5` and every C dependency from source with AddressSanitizer, LeakSanitizer and
UndefinedBehaviorSanitizer (the hermetic LLVM toolchain's `@llvm//config:asan` and `@llvm//config:ubsan` flags;
UBSan aborts on the first finding). It is Linux-only and sets `ASAN_OPTIONS`, `LSAN_OPTIONS` and `UBSAN_OPTIONS`
for tests, so no manual steps are needed:

```bash
bazel test --config=asan //e2e:lock_direct_test //e2e:lock_layers_test //e2e:lock_features_test //e2e:oracle_linux_curl_mark_test //e2e:test_tools_test //e2e:dnf5_smoke_test //e2e:dnf5_config_smoke_test //e2e:dnf5_config_golden_test //e2e:nsvcap_test //e2e/testdata:lock_golden_tests
```

Known third-party noise is suppressed in [`sanitizers/ubsan.supp`](sanitizers/ubsan.supp) and
[`sanitizers/lsan.supp`](sanitizers/lsan.supp); both ship to the tests as data only under this config. Add an
entry there, with a reason, rather than weakening the options. The `Sanitizers` workflow runs exactly this on every pull request and push to `main`.

## License

This project is licensed under the [Apache License, Version 2.0](LICENSE).

The Apache 2.0 license is a permissive open source license that allows free use, modification, reproduction, and distribution in both open source and private closed-source / commercial software projects.
