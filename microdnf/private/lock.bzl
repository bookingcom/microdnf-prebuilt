"""Implementation of rpm_lock rule."""

load("//microdnf/private:config.bzl", "dnf5_script", "render_dnf_config")
load("//microdnf/private:providers.bzl", "RpmLockFileInfo")

def _solver_args(ctx):
    """dnf_lock flags shaping the solve (dnf semantics), shared by build and run."""
    args = []
    if ctx.attr.forcearch:
        args.append("--forcearch=" + ctx.attr.forcearch)
    for pattern in ctx.attr.excludepkgs:
        args.append("--excludepkgs=" + pattern)
    for spec in ctx.attr.module_enable:
        args.append("--module-enable=" + spec)
    if ctx.attr.module_platform_id:
        args.append("--setopt=module_platform_id=" + ctx.attr.module_platform_id)
    if ctx.attr.nobest:
        args.append("--nobest")
    if not ctx.attr.install_weak_deps:
        args.append("--setopt=install_weak_deps=False")
    return args

def _dnf5_lock_args(ctx, lock_files):
    """Arguments of `dnf5 lock` but the output; the solver settings are in the dnf_config files."""
    args = ["--archlist=" + ",".join(ctx.attr.arch)]
    args += ["--prune=" + pkg for pkg in ctx.attr.exclude]
    args += ["--module-enable=" + spec for spec in ctx.attr.module_enable]
    args += ["--lockfile=" + lf for lf in lock_files]
    return args + ctx.attr.packages

def _dnf5_lock(ctx, out, repo_rel_dir):
    """The dnf5 backend of rpm_lock: the same lockfile, solved by `dnf5 lock` instead of dnf_lock."""
    dnf5 = ctx.executable.dnf5_tool
    if not dnf5:
        fail("rpm_lock %s: backend = \"dnf5\" needs dnf5_tool" % ctx.label)
    info = render_dnf_config(
        ctx,
        ctx.label.name + "_dnf5",
        [ctx.file.repo_config],
        ctx.files.repo_files,
        ctx.attr.arch,
        ctx.attr.releasever,
        forcearch = ctx.attr.forcearch,
        nobest = ctx.attr.nobest,
        install_weak_deps = ctx.attr.install_weak_deps,
        excludepkgs = ctx.attr.excludepkgs,
        module_platform_id = ctx.attr.module_platform_id,
        module_enable = ctx.attr.module_enable,
    )

    build_script = ctx.actions.declare_file(ctx.label.name + "_build.sh")
    ctx.actions.write(build_script, dnf5_script(dnf5, info, repo_rel_dir, "lock", lambda f: f.path), is_executable = True)
    args = ctx.actions.args()
    args.add("--output=" + out.path)
    args.add_all(_dnf5_lock_args(ctx, [lf.path for lf in ctx.files.lock_files]))
    ctx.actions.run(
        mnemonic = "RpmLock",
        progress_message = "Generating RPM lockfile %s with dnf5" % out.short_path,
        executable = build_script,
        arguments = [args],
        inputs = depset(ctx.files.lock_files, transitive = [info.files]),
        tools = [dnf5],
        outputs = [out],
    )

    # 'bazel run' writes the lockfile into the source tree.
    out_dest = ctx.attr.out.name if ctx.attr.out else ctx.label.name + ".lock.json"
    runner_script = ctx.actions.declare_file(ctx.label.name + "_run.sh")
    solve = ctx.actions.declare_file(ctx.label.name + "_solve.sh")
    ctx.actions.write(solve, dnf5_script(dnf5, info, repo_rel_dir, "lock", lambda f: f.short_path), is_executable = True)
    ctx.actions.write(runner_script, """#!/usr/bin/env bash
set -euo pipefail

DEST="${BUILD_WORKSPACE_DIRECTORY}/%s/%s"
TMP_OUT="$(mktemp)"
trap 'rm -f "${TMP_OUT}"' EXIT

"%s" --output="${TMP_OUT}" %s

mkdir -p "$(dirname "${DEST}")"
cp -f "${TMP_OUT}" "${DEST}"
echo "Lockfile updated at: ${DEST}"
""" % (
        ctx.label.package,
        out_dest,
        solve.short_path,
        " ".join(["'%s'" % a for a in _dnf5_lock_args(ctx, [lf.short_path for lf in ctx.files.lock_files])]),
    ), is_executable = True)

    return [
        DefaultInfo(
            files = depset([out]),
            executable = runner_script,
            runfiles = ctx.runfiles(files = [solve, dnf5] + ctx.files.lock_files, transitive_files = info.files),
        ),
        RpmLockFileInfo(
            lock_file = out,
            arch = ",".join(ctx.attr.arch),
            packages = ctx.attr.packages,
        ),
    ]

def _rpm_lock_impl(ctx):
    out = ctx.outputs.out
    if not out:
        out = ctx.actions.declare_file(ctx.label.name + ".lock.json")

    build_script = ctx.actions.declare_file(ctx.label.name + "_build.sh")
    runner_script = ctx.actions.declare_file(ctx.label.name + "_run.sh")

    repo_conf = ctx.file.repo_config
    lock_tool = ctx.executable.lock_tool
    repo_files = ctx.files.repo_files

    # Determine relative repo directory by finding the parent of repodata
    repo_rel_dir = ""
    for f in repo_files:
        if "/repodata/" in f.path or f.path.endswith("/repodata"):
            parts = f.path.split("/repodata")
            repo_rel_dir = parts[0]
            break
    if not repo_rel_dir and repo_files:
        repo_rel_dir = repo_files[0].dirname

    if ctx.attr.backend == "dnf5":
        return _dnf5_lock(ctx, out, repo_rel_dir)

    # Script for hermetic bazel build
    build_content = """#!/usr/bin/env bash
set -euo pipefail

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

REPOS_DIR="${TMP_DIR}/repos.d"
mkdir -p "${REPOS_DIR}"

if grep -q '@REPO_DIR@' "%s"; then
  sed "s|@REPO_DIR@|${PWD}/%s|g" "%s" > "${REPOS_DIR}/repo.repo"
else
  cp "%s" "${REPOS_DIR}/repo.repo"
fi

CACHE_DIR="${TMP_DIR}/cache"
mkdir -p "${CACHE_DIR}"

"%s" \\
  --reposdir="${REPOS_DIR}" \\
  --setopt=cachedir="${CACHE_DIR}" \\
  "$@"
""" % (repo_conf.path, repo_rel_dir, repo_conf.path, repo_conf.path, lock_tool.path)

    ctx.actions.write(
        output = build_script,
        content = build_content,
        is_executable = True,
    )

    arch_str = ",".join(ctx.attr.arch)

    args = ctx.actions.args()
    args.add("--output=" + out.path)
    args.add("--archlist=" + arch_str)
    args.add("--releasever=" + ctx.attr.releasever)

    for exc in ctx.attr.exclude:
        args.add("--exclude=" + exc)

    for lf in ctx.files.lock_files:
        args.add("--lockfile=" + lf.path)

    solver_args = _solver_args(ctx)
    args.add_all(solver_args)

    for pkg in ctx.attr.packages:
        args.add(pkg)

    inputs = depset(
        direct = [repo_conf] + repo_files + ctx.files.lock_files,
        transitive = [],
    )

    ctx.actions.run(
        mnemonic = "RpmLock",
        progress_message = "Generating RPM lockfile %s" % out.short_path,
        executable = build_script,
        arguments = [args],
        inputs = inputs,
        tools = [lock_tool],
        outputs = [out],
    )

    # Runner script for 'bazel run' updating the lockfile in source tree
    out_dest = ctx.attr.out.name if ctx.attr.out else ctx.label.name + ".lock.json"
    run_content = """#!/usr/bin/env bash
set -euo pipefail

DEST="${BUILD_WORKSPACE_DIRECTORY}/%s/%s"
TMP_OUT="$(mktemp)"
trap 'rm -f "${TMP_OUT}"' EXIT

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"; rm -f "${TMP_OUT}"' EXIT

REPOS_DIR="${TMP_DIR}/repos.d"
mkdir -p "${REPOS_DIR}"

CONF_PATH="%s"
REPO_REL="%s"

if grep -q '@REPO_DIR@' "${CONF_PATH}"; then
  sed "s|@REPO_DIR@|${PWD}/${REPO_REL}|g" "${CONF_PATH}" > "${REPOS_DIR}/repo.repo"
else
  cp "${CONF_PATH}" "${REPOS_DIR}/repo.repo"
fi

CACHE_DIR="${TMP_DIR}/cache"
mkdir -p "${CACHE_DIR}"

"%s" \\
  --reposdir="${REPOS_DIR}" \\
  --setopt=cachedir="${CACHE_DIR}" \\
  --output="${TMP_OUT}" \\
  --archlist="%s" \\
  --releasever="%s" \\
  %s \\
  %s \\
  %s \\
  %s

mkdir -p "$(dirname "${DEST}")"
cp -f "${TMP_OUT}" "${DEST}"
echo "Lockfile updated at: ${DEST}"
""" % (
        ctx.label.package,
        out_dest,
        repo_conf.short_path,
        repo_rel_dir,
        lock_tool.short_path,
        arch_str,
        ctx.attr.releasever,
        " ".join(["--exclude='%s'" % e for e in ctx.attr.exclude]),
        " ".join(["--lockfile='%s'" % lf.short_path for lf in ctx.files.lock_files]),
        " ".join(["'%s'" % a for a in solver_args]),
        " ".join(["'%s'" % p for p in ctx.attr.packages]),
    )

    ctx.actions.write(
        output = runner_script,
        content = run_content,
        is_executable = True,
    )

    runfiles = ctx.runfiles(files = [repo_conf, lock_tool] + repo_files + ctx.files.lock_files)

    return [
        DefaultInfo(
            files = depset([out]),
            executable = runner_script,
            runfiles = runfiles,
        ),
        RpmLockFileInfo(
            lock_file = out,
            arch = arch_str,
            packages = ctx.attr.packages,
        ),
    ]

rpm_lock = rule(
    implementation = _rpm_lock_impl,
    doc = "Generates a hermetic RPM JSON lockfile for specified packages and dependencies, solved by `dnf5 lock` " +
          "(backend = \"dnf5\", the default) or dnf_lock (backend = \"libdnf\").",
    attrs = {
        "arch": attr.string_list(
            mandatory = True,
            allow_empty = False,
            doc = "Mandatory target CPU architecture list (e.g. ['x86_64', 'noarch']).",
        ),
        "backend": attr.string(
            default = "dnf5",
            values = ["libdnf", "dnf5"],
            doc = "Solver behind the lockfile: `dnf5 lock` (dnf5_tool, the default) or dnf_lock (libdnf, lock_tool; " +
                  "deprecated, kept for one release). The lockfiles are the same.",
        ),
        "dnf5_tool": attr.label(
            default = Label("//microdnf:dnf5"),
            executable = True,
            cfg = "exec",
            doc = "dnf5 executable (with the lock command) for backend = \"dnf5\".",
        ),
        "exclude": attr.string_list(
            default = [],
            doc = "Optional packages to exclude and prune from the dependency tree.",
        ),
        "excludepkgs": attr.string_list(
            default = [],
            doc = "NEVRA globs hidden from the solver, like dnf excludepkgs= (e.g. release cut-offs).",
        ),
        "forcearch": attr.string(
            doc = "Architecture to solve for instead of the host's (default: first non-noarch entry of arch).",
        ),
        "install_weak_deps": attr.bool(
            default = True,
            doc = "Install weak dependencies (Recommends, matching Supplements), like dnf's install_weak_deps. False leaves them out.",
        ),
        "lock_files": attr.label_list(
            default = [],
            allow_files = [".json"],
            doc = "Ordered list of lock files from previous layers.",
        ),
        "lock_tool": attr.label(
            default = Label("//microdnf:dnf_lock"),
            executable = True,
            cfg = "exec",
            doc = "dnf_lock executable for backend = \"libdnf\".",
        ),
        "module_enable": attr.string_list(
            default = [],
            doc = "Module streams to enable before resolving (NAME:STREAM), like dnf module enable.",
        ),
        "module_platform_id": attr.string(
            doc = "Platform module (e.g. 'platform:el9') that module streams are resolved against.",
        ),
        "nobest": attr.bool(
            default = False,
            doc = "Allow older candidates when the newest requested package is not installable.",
        ),
        "out": attr.output(
            mandatory = False,
            doc = "Output JSON lockfile name.",
        ),
        "packages": attr.string_list(
            mandatory = True,
            allow_empty = False,
            doc = "Mandatory list of top-level RPM packages to resolve.",
        ),
        "releasever": attr.string(
            mandatory = True,
            doc = "Mandatory distribution release version (e.g. '9').",
        ),
        "repo_config": attr.label(
            mandatory = True,
            allow_single_file = [".repo", ".conf"],
            doc = "Mandatory repository configuration file.",
        ),
        "repo_files": attr.label_list(
            allow_files = True,
            doc = "Local repository metadata files (e.g. repodata files, repomd.xml).",
        ),
    },
    executable = True,
)
