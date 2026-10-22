"""Public API for generating RPM lockfiles."""

load("//rpm:providers.bzl", "RpmLockFileInfo")

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

"%s" \
  --reposdir="${REPOS_DIR}" \
  --setopt=cachedir="${CACHE_DIR}" \
  "$@"
""" % (repo_conf.path, repo_rel_dir, repo_conf.path, repo_conf.path, lock_tool.path)

    ctx.actions.write(
        output = build_script,
        content = build_content,
        is_executable = True,
    )

    args = ctx.actions.args()
    args.add("--releasever", ctx.attr.releasever)
    args.add("--archlist", ctx.attr.arch)
    args.add("--output", out)
    for ex in ctx.attr.exclude:
        args.add("--exclude", ex)
    for pkg in ctx.attr.packages:
        args.add(pkg)

    ctx.actions.run(
        inputs = [repo_conf] + repo_files,
        outputs = [out],
        executable = build_script,
        arguments = [args],
        tools = [lock_tool],
        mnemonic = "RpmLock",
        progress_message = "Generating RPM lockfile %{output}",
    )

    # Runner script for `bazel run`
    dest_path = ctx.outputs.out.short_path if ctx.outputs.out else ctx.label.name + ".lock.json"
    exclude_args = " ".join(['--exclude="%s"' % x for x in ctx.attr.exclude])
    pkg_args = " ".join(['"%s"' % p for p in ctx.attr.packages])

    run_content = """#!/usr/bin/env bash
set -euo pipefail

DEST="${BUILD_WORKSPACE_DIRECTORY}/%s"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

REPOS_DIR="${TMP_DIR}/repos.d"
mkdir -p "${REPOS_DIR}"
cp "%s" "${REPOS_DIR}/repo.repo"

CACHE_DIR="${TMP_DIR}/cache"
mkdir -p "${CACHE_DIR}"

"%s" \
  --reposdir="${REPOS_DIR}" \
  --setopt=cachedir="${CACHE_DIR}" \
  --releasever="%s" \
  --archlist="%s" \
  %s \
  --output="${DEST}" \
  %s

echo "Wrote lockfile to ${DEST}"
""" % (
        dest_path,
        repo_conf.short_path,
        lock_tool.short_path,
        ctx.attr.releasever,
        ctx.attr.arch,
        exclude_args,
        pkg_args,
    )

    ctx.actions.write(
        output = runner_script,
        content = run_content,
        is_executable = True,
    )

    runfiles = ctx.runfiles(files = [repo_conf, lock_tool])

    return [
        DefaultInfo(
            files = depset([out]),
            executable = runner_script,
            runfiles = runfiles,
        ),
        RpmLockFileInfo(
            lock_file = out,
            requested = ctx.attr.packages,
            arch = ctx.attr.arch,
            releasever = ctx.attr.releasever,
        ),
    ]

rpm_lock = rule(
    implementation = _rpm_lock_impl,
    doc = "Generates a lockfile out of target RPM packages, yum repository configuration, arch, releasever, and excludes.",
    executable = True,
    attrs = {
        "packages": attr.string_list(
            mandatory = True,
            doc = "List of target RPM packages or specs to resolve.",
        ),
        "repo_config": attr.label(
            mandatory = True,
            allow_single_file = [".repo"],
            doc = "Mandatory Yum repository configuration file (.repo).",
        ),
        "repo_files": attr.label_list(
            default = [],
            allow_files = True,
            doc = "Optional files inside local repositories (repodata, RPMs).",
        ),
        "arch": attr.string(
            mandatory = True,
            doc = "Mandatory target architecture (e.g. x86_64, aarch64).",
        ),
        "releasever": attr.string(
            mandatory = True,
            doc = "Mandatory operating system release version (e.g. 9, 10, 40).",
        ),
        "exclude": attr.string_list(
            default = [],
            doc = "List of packages to exclude and trim along with unneeded dependencies unless directly requested.",
        ),
        "out": attr.output(
            doc = "Optional explicit output lockfile path. Defaults to {name}.lock.json.",
        ),
        "lock_tool": attr.label(
            default = Label("//:dnf_lock"),
            executable = True,
            allow_single_file = True,
            cfg = "exec",
            doc = "Lock tool binary.",
        ),
    },
)
