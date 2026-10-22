"""Bazel rule and macro to extract files from an RPM archive, dereference symlinks, and mark executables."""

load("//rpm:providers.bzl", "RpmArchiveInfo", "RpmExtractedFilesInfo")

_EXTRACTOR_SCRIPT = """
import os
import sys
import tarfile

def resolve_member(tf, member, visited=None):
    if visited is None:
        visited = set()
    if member.name in visited:
        sys.exit(f"Cyclic symlink detected at {member.name}")
    visited.add(member.name)

    if member.issym() or member.islnk():
        target = member.linkname
        if not target.startswith("/"):
            target = os.path.normpath(os.path.join(os.path.dirname(member.name), target))
        target = target.lstrip("./").lstrip("/")

        # Search for target member in archive
        for candidate in ["./" + target, target, "/" + target]:
            try:
                m = tf.getmember(candidate)
                return resolve_member(tf, m, visited)
            except KeyError:
                continue

        # Try prefix matching
        for m in tf.getmembers():
            norm_m = m.name.lstrip("./").lstrip("/")
            if norm_m == target:
                return resolve_member(tf, m, visited)

        sys.exit(f"Symlink target {member.linkname} not found in archive")
    return member

def main():
    archive_path = sys.argv[1]
    # Remaining args: pairs of <req_path> <out_path> <is_exec: 0 or 1>
    args = sys.argv[2:]
    if len(args) % 3 != 0:
        sys.exit("Invalid extractor arguments")

    specs = []
    for i in range(0, len(args), 3):
        specs.append((args[i], args[i+1], args[i+2] == "1"))

    mode = "r:*"
    if archive_path.endswith(".tar.zst") or archive_path.endswith(".tzst"):
        # For zstd, decompress via zstandard or shell
        import subprocess
        p = subprocess.Popen(["zstd", "-d", "-c", archive_path], stdout=subprocess.PIPE)
        tf = tarfile.open(fileobj=p.stdout, mode="r|")
        # Extract streaming to temp or read
        members_map = {}
        for m in tf:
            f = tf.extractfile(m)
            members_map[m.name.lstrip("./").lstrip("/")] = (m, f.read() if f else b"")
        for req_path, out_path, is_exec in specs:
            norm_req = req_path.lstrip("./").lstrip("/")
            if norm_req not in members_map:
                sys.exit(f"File {req_path} not found in archive")
            _, content = members_map[norm_req]
            os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
            with open(out_path, "wb") as out_f:
                out_f.write(content)
            os.chmod(out_path, 0o755 if is_exec else 0o644)
        return

    with tarfile.open(archive_path, mode) as tf:
        members_by_norm = {}
        for m in tf.getmembers():
            members_by_norm[m.name.lstrip("./").lstrip("/")] = m

        for req_path, out_path, is_exec in specs:
            norm_req = req_path.lstrip("./").lstrip("/")
            target_member = None

            for candidate in ["./" + norm_req, norm_req, "/" + norm_req]:
                try:
                    target_member = tf.getmember(candidate)
                    break
                except KeyError:
                    pass

            if not target_member and norm_req in members_by_norm:
                target_member = members_by_norm[norm_req]

            if not target_member:
                sys.exit(f"Requested file '{req_path}' not found in archive '{archive_path}'")

            resolved = resolve_member(tf, target_member)
            extracted_file = tf.extractfile(resolved)
            content = extracted_file.read() if extracted_file else b""

            os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
            with open(out_path, "wb") as out_f:
                out_f.write(content)

            os.chmod(out_path, 0o755 if is_exec else 0o644)

if __name__ == "__main__":
    main()
"""

def _clean_path(path):
    p = path.strip()
    for prefix in ["./", "/"]:
        if p.startswith(prefix):
            p = p[len(prefix):]
    return p

def _rpm_extract_file_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)
    archive_file = ctx.file.archive
    arch = "noarch"
    if RpmArchiveInfo in ctx.attr.archive:
        arch = ctx.attr.archive[RpmArchiveInfo].arch

    is_exec = getattr(ctx.attr, "is_executable", False)

    script_file = ctx.actions.declare_file(ctx.label.name + "_extractor.py")
    ctx.actions.write(
        output = script_file,
        content = _EXTRACTOR_SCRIPT,
        is_executable = True,
    )

    args = ctx.actions.args()
    args.add(archive_file)
    args.add(ctx.attr.file)
    args.add(out)
    args.add("1" if is_exec else "0")

    ctx.actions.run(
        inputs = [archive_file],
        outputs = [out],
        executable = "python3",
        arguments = [script_file.path, args],
        tools = [script_file],
        mnemonic = "RpmExtract",
        progress_message = "Extracting %s from %s" % (ctx.attr.file, archive_file.short_path),
    )

    default_info_kwargs = {
        "files": depset([out]),
    }
    if is_exec:
        default_info_kwargs["executable"] = out

    return [
        DefaultInfo(**default_info_kwargs),
        RpmExtractedFilesInfo(
            files = depset([out]),
            executables = depset([out]) if is_exec else depset([]),
            arch = arch,
        ),
    ]

_rpm_extract_file = rule(
    implementation = _rpm_extract_file_impl,
    executable = False,
    attrs = {
        "archive": attr.label(mandatory = True, allow_single_file = True),
        "file": attr.string(mandatory = True),
        "is_executable": attr.bool(default = False),
    },
)

_rpm_extract_binary = rule(
    implementation = _rpm_extract_file_impl,
    executable = True,
    attrs = {
        "archive": attr.label(mandatory = True, allow_single_file = True),
        "file": attr.string(mandatory = True),
        "is_executable": attr.bool(default = True),
    },
)

def rpm_extract(name, archive, files, executables = [], **kwargs):
    """Extracts files from an archive, dereferences symlinks, and creates executable targets."""
    clean_execs = [_clean_path(e) for e in executables]
    extracted_targets = []

    for f in files:
        clean_f = _clean_path(f)
        is_exec = clean_f in clean_execs
        file_target_name = "%s_%s" % (name, clean_f.replace("/", "_"))

        if is_exec:
            _rpm_extract_binary(
                name = file_target_name,
                archive = archive,
                file = clean_f,
                is_executable = True,
                **kwargs
            )
        else:
            _rpm_extract_file(
                name = file_target_name,
                archive = archive,
                file = clean_f,
                is_executable = False,
                **kwargs
            )
        extracted_targets.append(":" + file_target_name)

        base = clean_f.split("/")[-1]
        short_name = "%s_%s" % (name, base)
        if short_name != file_target_name:
            native.alias(
                name = short_name,
                actual = ":" + file_target_name,
                **kwargs
            )

    native.filegroup(
        name = name,
        srcs = extracted_targets,
        **kwargs
    )
