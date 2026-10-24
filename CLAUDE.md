# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Read [`agents.md`](agents.md) first: it holds the binding rules (never touch upstream, immutable release tags,
release tarball contents, `dnf_lock` naming, Bazel invocation-ID protocol, RE2-only Renovate regexes,
Europe/Amsterdam timestamps). They are not repeated here. User-facing behaviour of `dnf_lock` (`--installroot`,
`--excludepkgs`, modules, best, weak deps, `--forcearch`) is documented in [`README.md`](README.md).

## Commands

Every Bazel call goes through a UUID invocation ID (see `agents.md` §4):

```bash
INVOCATION_ID=$(uuidgen) && echo "Invocation ID: ${INVOCATION_ID}" && bazel build --invocation_id=${INVOCATION_ID} //:microdnf //:rpm2archive //:dnf_lock //:rpm2files
INVOCATION_ID=$(uuidgen) && echo "Invocation ID: ${INVOCATION_ID}" && bazel build --invocation_id=${INVOCATION_ID} //:microdnf_all //:rpm2archive_all //:dnf_lock_all //:rpm2files_all  # all 4 platforms
INVOCATION_ID=$(uuidgen) && echo "Invocation ID: ${INVOCATION_ID}" && bazel test --invocation_id=${INVOCATION_ID} //...
INVOCATION_ID=$(uuidgen) && echo "Invocation ID: ${INVOCATION_ID}" && bazel test --invocation_id=${INVOCATION_ID} //e2e:lock_features_test   # single test
```

- CI runs `bazel build //... <the four *_all targets> --config=ci` and then `bazel test //... --config=ci`.
  `--config=ci` uses the Docker sandbox (`gcr.io/flame-public/rbe-ubuntu24-04`), and `--config=rbe` is BuildBuddy remote execution.
- Tests are `sh_test`s in `e2e/BUILD.bazel`. Most take their inputs as `$(location …)` args, often a lockfile produced by an `rpm_lock` target in the same package.
- `//e2e:downstream_e2e_tests` (`rules_bazel_integration_test`) runs a nested Bazel 9.3.1 against `e2e/smoke_test/`.
  That workspace consumes this module through `local_path_override` and `tools.prebuilt(archive = "@microdnf_prebuilt//:microdnf_tools.tar.gz")`.
  It needs network, and it is excluded from the outer build via `--deleted_packages=e2e/smoke_test`.
- `--lockfile_mode=update` is set, so `MODULE.bazel.lock` changes whenever deps change. Commit it.

## Architecture

**Two module personas in one repo.** `MODULE.bazel` is the dev module. All C/C++ deps are `dev_dependency`.
The release tarball keeps only `microdnf/`, `microdnf/private/`, the root `BUILD.bazel` and a stripped
`MODULE.bazel`, so downstream users get rules and prebuilt binaries, never the compiler graph.
`.github/workflows/release_prep.sh` and `package_release.sh` do this stripping and regenerate
`microdnf/private/integrities.bzl` (release URLs and SRI hashes).

**Native code (`dnf/`).**
- Upstream microdnf sources come from `@microdnf_src`, a `git_repository` pinned in `bazel/extensions.bzl`.
  `dnf/BUILD.bazel` copies upstream core files and plugins, then compiles them with local code.
- `lock.c` / `rpm2archive.c` are the shared implementations. Each is linked twice: into `microdnf` as a plugin
  (`plugins/{lock,rpm2archive}/`, with gresource-embedded `.plugin` files) and into a standalone binary
  (`lock-main.c` → `dnf_lock`, `rpm2archive-main.c` → `rpm2archive`).
- `lock-goal.cpp` is a thin C wrapper over libdnf's C++-only API (`LIBDNF_UNSTABLE_API`) used by `lock.c`.
- `rpm2files/` is a Go tool (rules_go; the SDK version comes from `go.mod`).
- Cross-platform outputs are `platform_transition_binary` targets to `//bazel:<platform>`. Root `BUILD.bazel` aliases them as `//:<tool>[_<platform>|_all]`.

**Local registry (`bazel/3rdparty/`).** It is consulted before BCR and holds source-built C deps: glib, rpm,
libdnf, libsolv, librepo, libmodulemd, gpgme, … Each is the upstream tarball plus an `overlay/` (BUILD files,
pre-generated `config.h`, patched sources) whose files are pinned by SRI hash in `source.json`.
Any change to an upstream dependency goes here, never upstream. Bump a module with `git mv` to
`<upstream_version>.bcr.<N+1>`, then patch, so the diff shows only the change; the full protocol (metadata,
`source.json` hashes, `single_version_override` instead of keeping the old version) is `agents.md` §6.
`2252303` (libgpg-error bcr.5) is a reference example.

**Public Starlark API (`microdnf/`).** The top-level `*.bzl` files re-export from `microdnf/private/`.
- `rpm_lock` (`private/lock.bzl`): a build action that runs `dnf_lock` against a `.repo` file (`@REPO_DIR@` is
  substituted with the repo's path) plus local repodata, and optionally an `installroot` filegroup copied into a
  writable scratch dir. The target is also `bazel run`-able to refresh a lockfile in place.
  `lock_tool` defaults to the source-built `//:dnf_lock`.
- `rpm` module extension → `rpm_repository`: reads lock JSON files and creates one `http_file`-backed
  `rpm_package` target per package, with arch `select`.
- `rpm_archive` (rpm2archive → layer tarball) and `rpm_extract` (rpm2files → individual files, symlinks dereferenced, executables marked).
- `microdnf_prebuilt_extension`: `http_file` repos per tool and platform from `integrities.bzl`, plus host-alias repos
  (`@microdnf`, `@dnf_lock`, …). Toolchains are registered under `//microdnf:toolchain_type`.
  The `tools.prebuilt` tag instead unpacks a local `microdnf_tools.tar.gz` (the `//:tools_archive` genrule).

**Test data (`e2e/testdata/`).**
- Repodata is generated at build time by `generate_repodata.py`: `test.repo` covers real RPMs, and the `features`
  scenario is a metadata-only repo with `modules.yaml`.
- `installroot/var/lib/rpm/rpmdb.sqlite` is a checked-in seeded rpmdb. Regenerate it with `make_installroot_rpmdb.sh`
  (needs Docker and an EL9 image) whenever the installed-package set the tests expect changes.
