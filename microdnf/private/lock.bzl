"""Implementation of rpm_lock rule."""

load("//microdnf/private:providers.bzl", "RpmLockFileInfo")

def _solver_args(ctx):
    """dnf_lock flags shaping the solve (dnf semantics), shared by build and run."""
    args = []
    if ctx.attr.forcearch:
        args.append("--forcearch=" + ctx.attr.forcearch)
    return args

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
    doc = "Generates a hermetic RPM JSON lockfile for specified packages and dependencies.",
    attrs = {
        "arch": attr.string_list(
            mandatory = True,
            allow_empty = False,
            doc = "Mandatory target CPU architecture list (e.g. ['x86_64', 'noarch']).",
        ),
        "exclude": attr.string_list(
            default = [],
            doc = "Optional packages to exclude and prune from the dependency tree.",
        ),
        "forcearch": attr.string(
            doc = "Architecture to solve for instead of the host's (default: first non-noarch entry of arch).",
        ),
        "lock_files": attr.label_list(
            default = [],
            allow_files = [".json"],
            doc = "Ordered list of lock files from previous layers.",
        ),
        "lock_tool": attr.label(
            default = Label("//:dnf_lock"),
            executable = True,
            cfg = "exec",
            doc = "Underlying lock tool executable.",
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
