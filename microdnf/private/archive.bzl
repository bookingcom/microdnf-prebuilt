"""Bazel rule to create an archive out of RPM targets and transitive dependencies."""

load("//microdnf/private:providers.bzl", "RpmArchiveInfo", "RpmInfo")

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

    obs_depsets = []
    for r in ctx.attr.obsolete_rpms:
        if RpmInfo in r:
            obs_depsets.append(r[RpmInfo].transitive_rpms)
        else:
            obs_depsets.append(r[DefaultInfo].files)
    obs_files = depset(transitive = obs_depsets).to_list()
    whiteouts = ctx.attr.whiteouts

    if not all_rpms and not obs_files and not whiteouts:
        fail("rpm_archive '%s' has no input RPM files from %s and no obsolete_rpms/whiteouts" % (ctx.label.name, ctx.attr.rpms))

    args = ctx.actions.args()
    args.add("--output", out)
    if compression:
        args.add("--compression", compression)
    for obs in obs_files:
        args.add("--obsolete-rpm", obs)
    for wh in whiteouts:
        args.add("--whiteout", wh)
    for f in all_rpms:
        args.add(f)

    ctx.actions.run(
        inputs = all_rpms + obs_files,
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
            default = [],
            providers = [RpmInfo],
            doc = "Set of RPM targets from proxy repo.",
        ),
        "obsolete_rpms": attr.label_list(
            default = [],
            allow_files = [".rpm"],
            doc = "Set of obsoleted/removed RPM packages to generate OCI whiteout deletion trackers for.",
        ),
        "whiteouts": attr.string_list(
            default = [],
            doc = "Explicit list of paths to whiteout in the layer archive.",
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
            default = Label("//microdnf:rpm2archive"),
            executable = True,
            cfg = "exec",
            doc = "rpm2archive binary tool.",
        ),
    },
)
