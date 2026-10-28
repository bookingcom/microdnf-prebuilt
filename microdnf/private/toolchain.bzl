"""Toolchain definition for microdnf and rpm2archive binaries."""

MicrodnfInfo = provider(
    doc = "Information about the prebuilt microdnf and rpm2archive tools.",
    fields = {
        "microdnf": "Target executable for microdnf.",
        "rpm2archive": "Target executable for rpm2archive.",
        "tool_files": "Depset of all tool files needed at execution time.",
    },
)

def _to_manifest_path(ctx, file):
    if file.short_path.startswith("../"):
        return "external/" + file.short_path[3:]
    return ctx.workspace_name + "/" + file.short_path

def _microdnf_toolchain_impl(ctx):
    tool_files = []
    if ctx.attr.microdnf:
        tool_files.extend(ctx.attr.microdnf.files.to_list())
    if ctx.attr.rpm2archive:
        tool_files.extend(ctx.attr.rpm2archive.files.to_list())

    microdnf_file = ctx.files.microdnf[0] if ctx.files.microdnf else None
    rpm2archive_file = ctx.files.rpm2archive[0] if ctx.files.rpm2archive else None

    microdnf_info = MicrodnfInfo(
        microdnf = microdnf_file,
        rpm2archive = rpm2archive_file,
        tool_files = depset(tool_files),
    )

    template_vars = {}
    if microdnf_file:
        template_vars["MICRODNF_BIN"] = microdnf_file.path
    if rpm2archive_file:
        template_vars["RPM2ARCHIVE_BIN"] = rpm2archive_file.path

    return [
        platform_common.ToolchainInfo(
            microdnf_info = microdnf_info,
        ),
        platform_common.TemplateVariableInfo(template_vars),
        DefaultInfo(
            files = depset(tool_files),
            runfiles = ctx.runfiles(files = tool_files),
        ),
    ]

microdnf_toolchain = rule(
    implementation = _microdnf_toolchain_impl,
    attrs = {
        "microdnf": attr.label(
            doc = "The microdnf executable binary.",
            mandatory = False,
            allow_files = True,
            executable = True,
            cfg = "exec",
        ),
        "rpm2archive": attr.label(
            doc = "The rpm2archive executable binary.",
            mandatory = False,
            allow_files = True,
            executable = True,
            cfg = "exec",
        ),
    },
    doc = "Defines a microdnf toolchain containing microdnf and rpm2archive binaries.",
)
