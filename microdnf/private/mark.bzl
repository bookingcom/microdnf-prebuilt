"""Public API for marking RPM packages into rpmdb."""

load("//microdnf/private:config.bzl", "dnf5_script", "render_dnf_config")

def _dnf5_mark(ctx, out_dir):
    """The dnf5 backend of rpm_mark: the same rpmdb, written by `dnf5 rpmdb-mark` instead of mark."""
    dnf5 = ctx.executable.dnf5_tool
    if not dnf5:
        fail("rpm_mark %s: backend = \"dnf5\" needs dnf5_tool" % ctx.label)
    arch = ctx.attr.arch.split(",")
    info = render_dnf_config(
        ctx,
        ctx.label.name + "_dnf5",
        [ctx.file.repo_config],
        ctx.files.repo_files,
        arch,
        ctx.attr.releasever,
    )
    script = ctx.actions.declare_file(ctx.label.name + "_mark.sh")
    ctx.actions.write(script, dnf5_script(dnf5, info, info.repo_dir, "rpmdb-mark", lambda f: f.path), is_executable = True)

    args = ctx.actions.args()
    args.add("--rpmdb-root=" + out_dir.path)
    args.add("--archlist=" + ",".join(arch))
    if ctx.attr.nodeps:
        args.add("--nodeps")
    args.add_all(ctx.attr.packages)
    ctx.actions.run(
        inputs = info.files,
        outputs = [out_dir],
        executable = script,
        arguments = [args],
        tools = [dnf5],
        mnemonic = "RpmMark",
        progress_message = "Marking RPM packages into rpmdb %{output} with dnf5",
    )
    return [DefaultInfo(files = depset([out_dir]))]

def _rpm_mark_impl(ctx):
    out_dir = ctx.actions.declare_directory(ctx.label.name)
    if ctx.attr.backend == "dnf5":
        return _dnf5_mark(ctx, out_dir)
    build_script = ctx.actions.declare_file(ctx.label.name + "_mark.sh")

    repo_conf = ctx.file.repo_config
    mark_tool = ctx.executable.mark_tool
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

mkdir -p "%s"

"%s" \\
  --reposdir="${REPOS_DIR}" \\
  --setopt=cachedir="${CACHE_DIR}" \\
  --installroot="%s" \\
  --releasever="%s" \\
  --archlist="%s" \\
  "$@"
""" % (repo_conf.path, repo_rel_dir, repo_conf.path, repo_conf.path, out_dir.path, mark_tool.path, out_dir.path, ctx.attr.releasever, ctx.attr.arch)

    ctx.actions.write(
        output = build_script,
        content = build_content,
        is_executable = True,
    )

    args = ctx.actions.args()
    if ctx.attr.nodeps:
        args.add("--nodeps")
    for pkg in ctx.attr.packages:
        args.add(pkg)

    ctx.actions.run(
        inputs = [repo_conf] + repo_files,
        outputs = [out_dir],
        executable = build_script,
        arguments = [args],
        tools = [mark_tool],
        mnemonic = "RpmMark",
        progress_message = "Marking RPM packages into rpmdb %{output}",
    )

    return [
        DefaultInfo(files = depset([out_dir])),
    ]

rpm_mark = rule(
    implementation = _rpm_mark_impl,
    attrs = {
        "backend": attr.string(
            default = "libdnf",
            values = ["libdnf", "dnf5"],
            doc = "Tool behind the rpmdb: mark (libdnf) or `dnf5 rpmdb-mark` (needs dnf5_tool). The rpmdbs hold the same packages.",
        ),
        "dnf5_tool": attr.label(
            executable = True,
            cfg = "exec",
            doc = "dnf5 executable (with the rpmdb-mark command) for backend = \"dnf5\".",
        ),
        "packages": attr.string_list(
            mandatory = True,
            doc = "List of package specifications with optional versions (e.g. ['curl', 'libcurl=7.76.1']).",
        ),
        "repo_config": attr.label(
            mandatory = True,
            allow_single_file = True,
            doc = "Repository configuration file (.repo).",
        ),
        "repo_files": attr.label_list(
            allow_files = True,
            doc = "Repository files including RPMs and repodata metadata.",
        ),
        "releasever": attr.string(
            default = "9",
            doc = "Distribution release version.",
        ),
        "arch": attr.string(
            default = "x86_64,noarch",
            doc = "Comma-separated target architectures.",
        ),
        "nodeps": attr.bool(
            default = False,
            doc = "Whether to disable dependency resolution.",
        ),
        "mark_tool": attr.label(
            default = Label("//:mark"),
            executable = True,
            cfg = "exec",
            doc = "Underlying mark tool executable.",
        ),
    },
)
