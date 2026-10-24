"""Implementation of rpm_lock rule."""

load("//microdnf/private:providers.bzl", "RpmLockFileInfo")

# Image-relative anchors that identify an installroot.
_ROOT_DIR_ANCHORS = ["var/lib/rpm/", "usr/lib/sysimage/rpm/", "etc/dnf/modules.d/"]
_ROOT_FILE_ANCHORS = ["etc/os-release", "usr/lib/os-release"]

def _quote(s):
    """Single-quotes `s` for a POSIX shell (bazel_skylib is a dev dependency, so not used here)."""
    return "'" + s.replace("'", "'\\''") + "'"

def _anchor_index(path):
    """Index of the earliest anchor in "/<short_path>", or -1. File anchors must end the path."""
    best = -1
    for anchor in _ROOT_DIR_ANCHORS + _ROOT_FILE_ANCHORS:
        idx = path.find("/" + anchor)
        if idx < 0 or (anchor in _ROOT_FILE_ANCHORS and not path.endswith("/" + anchor)):
            continue
        if best < 0 or idx < best:
            best = idx
    return best

def _installroot_layout(files, short):
    """Derives the image root from anchor paths such as var/lib/rpm/ or etc/os-release.

    Returns a struct(root, is_dir, entries) where entries is a list of
    (file, image_relative_path) for the anchored files. root is None when no
    installroot is given.
    """
    if not files:
        return struct(root = None, is_dir = False, entries = [])
    if any([f.is_directory for f in files]):
        if len(files) != 1:
            fail("rpm_lock installroot: a directory cannot be combined with other files: %s" %
                 [f.short_path for f in files])
        return struct(root = files[0].short_path if short else files[0].path, is_dir = True, entries = [])

    root = None
    first = None
    entries = []
    for f in files:
        # Anchors match short_path: identical for source and generated files of a package.
        path = "/" + f.short_path
        best = _anchor_index(path)
        if best < 0:
            continue
        this_root = path[1:best + 1].rstrip("/") if best > 0 else ""
        rel = path[best + 1:]
        if first == None:
            root = this_root
            first = path[1:]
        elif this_root != root:
            fail("rpm_lock installroot: anchored files disagree on the image root: %s (root %r) vs %s (root %r)" %
                 (first, root, path[1:], this_root))
        entries.append((f, rel))
    if not entries:
        fail("rpm_lock installroot: no file under %s or named %s was found in %s; pass the image's rpmdb/os-release files or a directory." %
             (_ROOT_DIR_ANCHORS, _ROOT_FILE_ANCHORS, [f.short_path for f in files]))
    return struct(root = root, is_dir = False, entries = entries)

def _installroot_snippet(files, short):
    """Shell that stages a writable installroot and sets INSTALLROOT_ARGS. Shared by build and run scripts."""
    layout = _installroot_layout(files, short)
    if layout.root == None:
        return "INSTALLROOT_ARGS=()"
    lines = [
        "INSTALLROOT_ARGS=()",
        "# Writable copy of the anchored files only: librpm opens the sqlite rpmdb in WAL mode.",
        'mkdir -p "${TMP_DIR}/installroot/usr"',
    ]
    if layout.is_dir:
        for sub in [a.rstrip("/") for a in _ROOT_DIR_ANCHORS] + _ROOT_FILE_ANCHORS:
            src = _quote(layout.root + "/" + sub)
            dst = '"${TMP_DIR}/installroot/%s"' % sub
            lines.append("if [ -e %s ]; then mkdir -p \"$(dirname %s)\"; cp -RL %s %s; fi" % (src, dst, src, dst))
    else:
        for f, rel in layout.entries:
            dst = '"${TMP_DIR}/installroot/%s"' % rel
            lines.append("mkdir -p \"$(dirname %s)\" && cp -L %s %s && chmod u+w %s" % (
                dst,
                _quote(f.short_path if short else f.path),
                dst,
                dst,
            ))
    lines.append("# librpm's default dbpath is /var/lib/rpm; distros symlink it to usr/lib/sysimage/rpm.")
    lines.append('if [ -d "${TMP_DIR}/installroot/usr/lib/sysimage/rpm" ] && [ ! -e "${TMP_DIR}/installroot/var/lib/rpm" ]; then')
    lines.append('  mkdir -p "${TMP_DIR}/installroot/var/lib" && ln -s ../../usr/lib/sysimage/rpm "${TMP_DIR}/installroot/var/lib/rpm"')
    lines.append("fi")
    lines.append('chmod -R u+w "${TMP_DIR}/installroot"')
    lines.append('INSTALLROOT_ARGS+=(--installroot="${TMP_DIR}/installroot")')
    return "\n".join(lines)

def _repos_snippet(conf, repo_dir):
    """Shell writing ${REPOS_DIR}/repo.repo (substituting @REPO_DIR@) and creating CACHE_DIR; needs TMP_DIR.

    `conf` and `repo_dir` are already shell-quoted. Shared by build and run scripts.
    """
    return """REPOS_DIR="${TMP_DIR}/repos.d"
mkdir -p "${REPOS_DIR}"

CONF_PATH=%s
REPO_REL=%s

if grep -q '@REPO_DIR@' "${CONF_PATH}"; then
  sed "s|@REPO_DIR@|${PWD}/${REPO_REL}|g" "${CONF_PATH}" > "${REPOS_DIR}/repo.repo"
else
  cp "${CONF_PATH}" "${REPOS_DIR}/repo.repo"
fi

CACHE_DIR="${TMP_DIR}/cache"
mkdir -p "${CACHE_DIR}"
""" % (conf, repo_dir)

def _repo_dir(repo_files, short):
    """Directory holding repodata/ (or the first file's directory), as path or short_path."""
    paths = [f.short_path if short else f.path for f in repo_files]
    for path in paths:
        if "/repodata/" in path or path.endswith("/repodata"):
            return path.split("/repodata")[0]
    return paths[0].rpartition("/")[0] if paths else ""

def _solver_args(ctx):
    """dnf_lock flags shaping the solve (dnf semantics), shared by build and run."""
    args = []
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
    for opt in ctx.attr.setopt:
        args.append("--setopt=" + opt)
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

    # Build actions run at the execroot (f.path), run scripts at the runfiles root (f.short_path).
    repo_rel_dir = _repo_dir(repo_files, short = False)
    repo_run_dir = _repo_dir(repo_files, short = True)

    installroot_files = ctx.files.installroot
    installroot_build = _installroot_snippet(installroot_files, short = False)
    installroot_run = _installroot_snippet(installroot_files, short = True)

    # Only anchored files (or the directory itself) are inputs.
    layout = _installroot_layout(installroot_files, short = False)
    installroot_inputs = installroot_files if layout.is_dir else [f for f, _ in layout.entries]

    # Script for hermetic bazel build
    build_content = """#!/usr/bin/env bash
set -euo pipefail

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

%s
%s

%s \\
  --reposdir="${REPOS_DIR}" \\
  --setopt=cachedir="${CACHE_DIR}" \\
  ${INSTALLROOT_ARGS[@]+"${INSTALLROOT_ARGS[@]}"} \\
  "$@"
""" % (_repos_snippet(_quote(repo_conf.path), _quote(repo_rel_dir)), installroot_build, _quote(lock_tool.path))

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

    solver_args = _solver_args(ctx)
    args.add_all(solver_args)

    args.add_all(ctx.attr.packages)

    inputs = depset([repo_conf] + repo_files + installroot_inputs)

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

DEST="${BUILD_WORKSPACE_DIRECTORY}"/%s
TMP_OUT="$(mktemp)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"; rm -f "${TMP_OUT}"' EXIT

%s
%s

%s \\
  --reposdir="${REPOS_DIR}" \\
  --setopt=cachedir="${CACHE_DIR}" \\
  ${INSTALLROOT_ARGS[@]+"${INSTALLROOT_ARGS[@]}"} \\
  --output="${TMP_OUT}" \\
  --archlist=%s \\
  --releasever=%s \\
  %s \\
  %s \\
  %s

mkdir -p "$(dirname "${DEST}")"
cp -f "${TMP_OUT}" "${DEST}"
echo "Lockfile updated at: ${DEST}"
""" % (
        _quote(ctx.label.package + "/" + out_dest),
        _repos_snippet(_quote(repo_conf.short_path), _quote(repo_run_dir)),
        installroot_run,
        _quote(lock_tool.short_path),
        _quote(arch_str),
        _quote(ctx.attr.releasever),
        " ".join([_quote("--exclude=" + e) for e in ctx.attr.exclude]),
        " ".join([_quote(a) for a in solver_args]),
        " ".join([_quote(p) for p in ctx.attr.packages]),
    )

    ctx.actions.write(
        output = runner_script,
        content = run_content,
        is_executable = True,
    )

    runfiles = ctx.runfiles(files = [repo_conf, lock_tool] + repo_files + installroot_inputs)

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
        "excludepkgs": attr.string_list(
            default = [],
            doc = "NEVRA globs hidden from the solver, like dnf excludepkgs= (e.g. release cut-offs).",
        ),
        "install_weak_deps": attr.bool(
            default = True,
            doc = "Install weak dependencies (Recommends/Supplements), like dnf install_weak_deps.",
        ),
        "installroot": attr.label(
            allow_files = True,
            doc = "Parent image root (a directory or its files) whose rpmdb and modules.d the " +
                  "lock resolves on top of, like dnf --installroot. Only the transaction is locked.",
        ),
        "module_enable": attr.string_list(
            default = [],
            doc = "Module streams to enable before resolving (NAME:STREAM), like dnf module enable.",
        ),
        "module_platform_id": attr.string(
            doc = "Platform module (e.g. 'platform:el9') when no installroot provides os-release.",
        ),
        "nobest": attr.bool(
            default = False,
            doc = "Allow older candidates when the newest requested package is not installable.",
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
        "setopt": attr.string_list(
            default = [],
            doc = "Extra dnf configuration overrides passed as --setopt=KEY=VALUE.",
        ),
        "repo_files": attr.label_list(
            allow_files = True,
            doc = "Local repository metadata files (e.g. repodata files, repomd.xml).",
        ),
    },
    executable = True,
)
