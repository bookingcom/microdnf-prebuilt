"""Bazel rule to create an archive out of RPM targets and transitive dependencies."""

load("//rpm:providers.bzl", "RpmArchiveInfo", "RpmInfo")

def _rpm_archive_impl(ctx):
    compression = ctx.attr.compression
    out = ctx.outputs.out
    if not out:
        ext = ".tar.gz"
        if compression in ["zstd", "zst"]:
            ext = ".tar.zst"
        elif compression == "none":
            ext = ".tar"
        out = ctx.actions.declare_file(ctx.label.name + ext)

    # Validate and propagate architecture
    resolved_arch = "noarch"
    for r in ctx.attr.rpms:
        p_arch = r[RpmInfo].arch
        if p_arch and p_arch != "noarch":
            if resolved_arch == "noarch":
                resolved_arch = p_arch
            elif resolved_arch != p_arch:
                fail("rpm_archive '%s' has conflicting architectures: '%s' vs '%s'" % (
                    ctx.label.name,
                    resolved_arch,
                    p_arch,
                ))

    # Collect transitive RPM files in dependency order
    rpm_depsets = [r[RpmInfo].transitive_rpms for r in ctx.attr.rpms]
    all_rpms_depset = depset(transitive = rpm_depsets, order = "postorder")
    all_rpms = all_rpms_depset.to_list()

    if not all_rpms:
        fail("rpm_archive '%s' has no input RPM files from %s" % (ctx.label.name, ctx.attr.rpms))

    args = ctx.actions.args()
    args.add("--output", out)
    if compression:
        args.add("--compression", compression)
    for f in all_rpms:
        args.add(f)

    ctx.actions.run(
        inputs = all_rpms,
        outputs = [out],
        executable = ctx.executable.rpm2archive_tool,
        arguments = [args],
        mnemonic = "Rpm2Archive",
        progress_message = "Creating archive %{output} from RPM targets",
    )

    return [
        DefaultInfo(files = depset([out])),
        RpmArchiveInfo(
            archive = out,
            arch = resolved_arch,
            rpms = all_rpms_depset,
        ),
    ]

rpm_archive = rule(
    implementation = _rpm_archive_impl,
    doc = "Creates an archive (tar, tar.gz, tar.zst) out of a set of RPM targets and their transitive dependencies.",
    attrs = {
        "rpms": attr.label_list(
            mandatory = True,
            providers = [RpmInfo],
            doc = "Set of RPM targets from proxy repo.",
        ),
        "compression": attr.string(
            default = "gzip",
            values = ["gzip", "zstd", "none", "auto"],
            doc = "Archive compression format.",
        ),
        "out": attr.output(
            doc = "Optional output archive file. Defaults to {name}.tar.gz (or .tar.zst / .tar).",
        ),
        "rpm2archive_tool": attr.label(
            default = Label("//:rpm2archive"),
            executable = True,
            allow_single_file = True,
            cfg = "exec",
            doc = "rpm2archive binary tool.",
        ),
    },
)
