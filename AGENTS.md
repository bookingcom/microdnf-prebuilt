# Agent Guidelines & Repository Learnings

This document captures critical architectural patterns, protocols, and operational learnings for AI agents and maintainers working on `microdnf-prebuilt`.

---

## 0. Strict Upstream Dependency & Repository Boundary Rule

> [!CAUTION]
> **Never open a pull request against upstream `microdnf` (`rpm-software-management/microdnf`).**
> - **Zero Upstream PRs**: There must be **0 upstream PRs** at all times. Never push branches or open PRs against `rpm-software-management/microdnf` or any other upstream repository. If an upstream PR is opened accidentally, it must be closed immediately.
> - **All PRs Target `microdnf_prebuilt`**: All pull requests must be opened exclusively against `microdnf_prebuilt` (`bookingcom/microdnf-prebuilt`).
> - **Never Modify Upstream Directly**: Changes MUST ONLY be made in `microdnf_prebuilt`. If any changes to upstream code (e.g. `microdnf`, `libdnf`, `rpm`, etc.) are necessary, they MUST be implemented as patches or overlays applied within this repository (e.g. under `bazel/3rdparty` or via Bazel patch attributes).

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
    - `defs.bzl`: Public macros and rule entrypoints (`rpm_archive`, `rpm_extract`, `rpm_lock`, etc.).
    - `extensions.bzl`: Public Bzlmod extension entrypoint.
    - `extension.bzl`: Public alias for `extensions.bzl`.
    - `*.bzl`: Feature module entrypoints (`archive.bzl`, `extract.bzl`, `lock.bzl`, `package.bzl`, `prebuilt.bzl`, `providers.bzl`, `repository.bzl`).
  - `microdnf/private/` directory (Internal implementation):
    - `BUILD.bazel`: Private package boundary.
    - `*.bzl`: Implementation files (`archive.bzl`, `extract.bzl`, `extensions.bzl`, `integrities.bzl`, `lock.bzl`, `package.bzl`, `platforms.bzl`, `prebuilt.bzl`, `providers.bzl`, `repository.bzl`, `toolchain.bzl`).
    - `integrities.bzl`: Dynamically generated file containing SRI hashes and URLs for prebuilt binaries.
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
4. **`rpm2files`**: Standalone Go-based tool for extracting files from RPM packages.
5. **`mark`**: Standalone tool resolving packages (and minimal dependencies) and importing RPM headers into an isolated SQLite `rpmdb`.

### Tool Naming Protocol
- The dependency solver is named **`dnf_lock`** across the entire repository (`//:dnf_lock`, `//dnf:dnf_lock`, `@dnf_lock//:dnf_lock`).
- **No compatibility aliases**: Do NOT introduce aliases for `lock`. Using `dnf_lock` avoids naming collisions with generic system lock utilities and accurately identifies the tool's purpose.

### Host Alias Repository Pattern
Modeled after `jq_host_alias_repo` in `bazel-contrib/jq.bzl`:
- Downstream consumers access host-executable tools directly via:
  - `@microdnf//:microdnf`
  - `@rpm2archive//:rpm2archive`
  - `@dnf_lock//:dnf_lock`
  - `@rpm2files//:rpm2files`
- Each repository rule (`microdnf_host_alias_repo`, `rpm2archive_host_alias_repo`, `dnf_lock_host_alias_repo`, `rpm2files_host_alias_repo`) inspects `rctx.os.name` and `rctx.os.arch` to alias directly to the matching `@<tool>_<platform>//file`.

### Upstream Repository Naming (`microdnf_src`)
- In `bazel/extensions.bzl`, the upstream source git repository is named **`microdnf_src`** (not `microdnf`).
- This prevents a Bzlmod repository name collision in `MODULE.bazel` with the `@microdnf` host alias repository.

---

## 4. Bazel Guidelines & Invocation Protocol

### Avoid `genrule`
- **Do not use `genrule`**: The use of `genrule` should always be avoided.
- **Check upstream rules first**: Always search the Bazel Central Registry (BCR) and canonical rulesets (e.g., `tar.bzl`, `rules_pkg`, `rules_cc`, `aspect_rules_py`, `bazel_skylib`, etc.) for existing rules that already cover the requirements before writing custom actions. Hermetic, structured rules must always be preferred over ad-hoc shell commands.

### Bazel Invocation Protocol

All Bazel commands (`build`, `test`, `run`, `cquery`, `query`, etc.) must be invoked with an explicitly generated UUID:

```bash
INVOCATION_ID=$(uuidgen) && echo "Invocation ID: ${INVOCATION_ID}" && bazel <command> --invocation_id=${INVOCATION_ID} <args>
```

### Remote Execution, Cache & Output Download Modes
- **Zero Disk Cache & Zero Repository Cache**: Local runner disk caching (`disk-cache: false`) and repository caching (`repository-cache: false`) are disabled entirely across CI and Release workflows. In addition, `build:rbe` sets `--disk_cache=` and `--repository_cache=`. This ensures all builds query and populate BuildBuddy remote cache and CAS consistently without runner disk caching divergence.
- **Remote Cache Chunking**: Enabled `--experimental_remote_cache_chunking` in `.bazelrc` and CI configuration to chunk large blobs via FastCDC, optimizing remote CAS storage and transfer efficiency.
- **Remote Repository Caching & Execution**: Enabled `startup --experimental_remote_repo_contents_cache` and `common --experimental_repo_remote_exec` to cache evaluated external repository contents remotely in CAS and allow remote execution of repository rules.
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

## 6. 3rdparty Module Version Bumping & Patching Protocol

When bumping or modifying a 3rdparty module under `bazel/3rdparty/modules/<module>/`:

### Move First, Patch Later (Preserve Git History & Clean Diffs)
- **Never duplicate version directories**: Do NOT create a new version directory by copying an existing version directory while leaving the old version behind (e.g., copying `1.51.0.bcr.4` to `1.51.0.bcr.5`). Duplicating version directories introduces thousands of redundant lines to pull requests, hides the actual patch diff, and complicates reviews.
- **Move first (`git mv`)**: Always move or rename the existing directory first (`git mv <old_version> <new_version>`).
- **Patch later**: Apply only the minimal necessary modifications (e.g., bug fixes, platform conditionals) on top of the moved directory. This ensures git detects the file renames and produces a concise diff showing only the actual changes.
- **Metadata and source integrity**:
  - Update `metadata.json` to reflect only the active version(s) in `versions`.
  - Update `MODULE.bazel` (both in the module root and overlay) with the bumped version.
  - Recalculate SRI hashes in `source.json` for any modified files.
- **Transitive dependency resolution**:
  - If other internal modules in `bazel/3rdparty/modules/` declare dependencies on the previous version, do not keep the old version directory around solely to satisfy them.
  - Instead, use `single_version_override(module_name = "<module>", version = "<new_version>")` in the root `MODULE.bazel` to enforce the new version across the dependency graph cleanly without cascading edits.

---

## 7. CLI Tool Design, Architecture Forcing & Memory Safety (PR #24 & PR #38 Learnings)

### Mandatory Architecture Override (`--forcearch` & `--archlist`)
> [!IMPORTANT]
> **Every command, CLI tool, or Bazel rule added to the repository that resolves, queries, or operates on architecture-aware RPM packages or metadata MUST provide a way to force the target architecture.**

- **Why Architecture Forcing Is Required**:
  - Builds and lockfile/rpmdb generation frequently cross-target a foreign CPU architecture (e.g., solving or marking `x86_64` RPMs on an `arm64` macOS or Linux host).
  - Filtering `HyQuery` by `--archlist` alone is **not sufficient**: `libdnf` / `libsolv` initializes its solver pool with the host architecture during `dnf_context_setup()`, causing the solver to reject foreign-architecture packages during `hy_goal_run_flags()`, and `$basearch` in `.repo` URLs defaults to the host CPU.
- **Implementation Protocol for `libdnf`-Based Tools (`dnf_lock`, `mark`, `microdnf`)**:
  1. **Expose `--forcearch=<arch>` and `--archlist=<arch,...>`** (with `--arch` supported as an alias where appropriate) on the CLI, and expose `forcearch` and `arch` attributes on the corresponding Bazel rules (`rpm_lock`, `rpm_mark`, `rpm_archive`, etc.).
  2. **Default Solve Architecture**: If `--forcearch` is not explicitly passed, default the solve architecture to the first non-`noarch`/`src` entry in `--archlist` / `--arch`.
  3. **Apply Before `dnf_context_setup()`**: Pre-parse `--forcearch` and `--archlist` / `--arch` before calling `dnf_context_setup(ctx, ...)` and set both:
     - `dnf_context_set_arch(ctx, solve_arch)` so `libsolv` solves for the target architecture.
     - `setenv("DNF_VAR_basearch", basearch_for_arch(solve_arch), 1)` (mapping `i?86` -> `i386`, `armv*hl` -> `armhfp`) so `$basearch` in repository configuration files expands to the target architecture.
- **CLI Flag Consistency Across Tools**:
  - `libdnf`-backed tools (`microdnf`, `dnf_lock`, `mark`) consistently support `--forcearch`, `--archlist` / `--arch`, `--config` / `--repo-config`, `--reposdir`, `--releasever`, `--installroot`, `--enablerepo`, `--disablerepo`, `--excludepkgs`, `--best` / `--nobest`, `--refresh`, and `--setopt=<opt>=<val>` (including `cachedir`, `reposdir`, `varsdir`, `module_platform_id`, `forcearch`, `excludepkgs`, and `<repo>.<option>`).
  - `rpm2archive` supports `--forcearch` and `--archlist` / `--arch` to validate input RPM headers (`RPMTAG_ARCH`) against the expected target architecture.
  - Dummy `GOptionEntry` tables used purely for `--help` output with `NULL` `arg_data` must pass only `{ argv[0], argv[in], NULL }` to `g_option_context_parse(help_ctx, ...)` so passing options alongside `--help` never dereferences a `NULL` `arg_data` pointer.

### GLib / `libdnf` / `rpm` Ownership & Sanitizer Learnings (PR #24 & PR #38)
- **`GOptionGroup` Ownership Transfer (Avoid Double-Unref / `heap-use-after-free`)**:
  - `g_option_context_add_group()` and `g_option_context_set_main_group()` **take ownership** of the `GOptionGroup*` passed to them, and `g_option_context_free()` unreferences those groups when the context is freed.
  - If a `GOptionGroup` is declared with `g_autoptr(GOptionGroup)`, you **must** transfer ownership via `g_steal_pointer(&group)` at every call site (e.g., `help_ctx`, `global_opt_ctx`, `subcmd_opt_ctx`), or use a plain `GOptionGroup*` pointer. Keeping the pointer in `g_autoptr` without `g_steal_pointer()` causes a double-unref (`heap-use-after-free` in `g_option_group_unref` under AddressSanitizer).
- **`GPtrArray` Free Functions for Ref-Counted Objects (Avoid LeakSanitizer Failures)**:
  - Whenever a helper creates a `GPtrArray*` and populates it with `g_object_ref()`'d objects (such as `DnfPackage*` in `select_one_pkg_for_nevra()`), construct the array with `g_ptr_array_new_full(size, g_object_unref)` or `g_ptr_array_new_with_free_func(g_object_unref)` rather than `g_ptr_array_sized_new()`.
  - Without a free function attached at construction, callers holding the returned array in `g_autoptr(GPtrArray)` free only the array wrapper and leak every `DnfPackage` reference.
- **`rpmfi` File Iterator Cleanup**:
  - `rpmfiArchiveClose(fi)` only closes the underlying payload stream; always call `rpmfiFree(fi)` afterward to free the `rpmfi` structure and its archive reader.
- **Target vs. Exec Configuration & Sanitizer Testing**:
  - Bazel builds tool dependencies configured with `cfg = "exec"` using `-c opt`, which defines `NDEBUG` and compiles out `assert()` checks in `libdnf` and `libsolv`.
  - Always test CLI binaries built in the **target** configuration as well (where assertions and sanitizers such as `ASan`, `LSan`, and `UBSan` are active) to catch assertion failures, use-after-free bugs, and memory leaks.

---

## 8. Timezone & Communication Guidelines

- **Timezone**: All timestamps, dates, and build times presented in reports, summaries, or commit notes must be localized to the Amsterdam timezone (`Europe/Amsterdam`, CET/CEST, UTC+1 / UTC+2).
- **File Links**: Always link to files and code symbols using GitHub-flavored markdown links (`[target](file:///path/to/target)`).
- **Conciseness**: Keep updates focused, structured, and informative.

---

## 9. Local Customizations

If `AGENTS.local.md` exists in the repository root, load and follow its instructions for user-specific overrides and configuration.

@[AGENTS.local.md](AGENTS.local.md)
