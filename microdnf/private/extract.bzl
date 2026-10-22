"""Bazel rule and macro to extract files from an RPM archive, dereference symlinks, and mark executables."""

load("//microdnf/private:providers.bzl", "RpmArchiveInfo", "RpmExtractedFilesInfo")

def _clean_path(path):
    p = path.strip()
    for prefix in ["./", "/"]:
        if p.startswith(prefix):
            p = p[len(prefix):]
    return p

def _rpm_extract_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)
    archive_file = ctx.file.archive
    arch = "noarch"
    if RpmArchiveInfo in ctx.attr.archive:
        arch = ctx.attr.archive[RpmArchiveInfo].arch

    is_exec = ctx.attr.is_executable
    tool = ctx.executable.rpm2files_tool

    # Write extraction specifications to an args file
    args_file = ctx.actions.declare_file(ctx.label.name + ".extract_args")
    ctx.actions.write(
        output = args_file,
        content = "%s %s %s\n" % (ctx.attr.file, out.path, "1" if is_exec else "0"),
    )

    args = ctx.actions.args()
    args.add("--archive", archive_file)
    args.add("--args-file", args_file)

    ctx.actions.run(
        inputs = [archive_file, args_file],
        outputs = [out],
        executable = tool,
        arguments = [args],
        mnemonic = "RpmExtract",
        progress_message = "Extracting %s from %s" % (ctx.attr.file, archive_file.short_path),
    )

    return [
        DefaultInfo(
            executable = out,
            files = depset([out]),
        ),
        RpmExtractedFilesInfo(
            files = depset([out]),
            executables = depset([out]) if is_exec else depset([]),
            arch = arch,
        ),
    ]

_rpm_extract = rule(
    implementation = _rpm_extract_impl,
    executable = True,
    attrs = {
        "archive": attr.label(
            mandatory = True,
            allow_single_file = True,
            doc = "The archive file (.tar, .tar.gz, .tar.zst) or target providing RpmArchiveInfo.",
        ),
        "file": attr.string(
            mandatory = True,
            doc = "Path of the file to extract from the archive.",
        ),
        "is_executable": attr.bool(
            default = False,
            doc = "Whether the extracted file is executable (0755) and recorded in executables.",
        ),
        "rpm2files_tool": attr.label(
            mandatory = True,
            executable = True,
            allow_single_file = True,
            cfg = "exec",
            doc = "The rpm2files extractor tool binary.",
        ),
    },
)

def rpm_extract(name, archive, files, executables = [], rpm2files_tool = None, **kwargs):
    """Extracts files from an archive, dereferences symlinks, and creates executable targets."""
    clean_execs = [_clean_path(e) for e in executables]
    extracted_targets = []
    tool = rpm2files_tool or Label("//microdnf:rpm2files")

    for f in files:
        clean_f = _clean_path(f)
        file_target_name = "%s/%s" % (name, clean_f)

        _rpm_extract(
            name = file_target_name,
            archive = archive,
            file = clean_f,
            is_executable = clean_f in clean_execs,
            rpm2files_tool = tool,
            **kwargs
        )
        extracted_targets.append(":" + file_target_name)

    native.filegroup(
        name = name,
        srcs = extracted_targets,
        **kwargs
    )
