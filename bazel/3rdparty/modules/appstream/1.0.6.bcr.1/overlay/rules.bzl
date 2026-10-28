"""Starlark rules for AppStream code generation."""

def _gperf_header_impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.out)
    args = ctx.actions.args()
    args.add(ctx.file.src.path)
    args.add("--output-file=" + out.path)
    ctx.actions.run(
        executable = ctx.executable._gperf,
        arguments = [args],
        inputs = [ctx.file.src],
        outputs = [out],
        mnemonic = "Gperf",
        progress_message = "Running gperf on %s" % ctx.file.src.short_path,
    )
    return [DefaultInfo(files = depset([out]))]

gperf_header = rule(
    implementation = _gperf_header_impl,
    attrs = {
        "out": attr.string(mandatory = True),
        "src": attr.label(mandatory = True, allow_single_file = [".gperf"]),
        "_gperf": attr.label(
            default = Label("@gperf//:gperf"),
            executable = True,
            cfg = "exec",
        ),
    },
)

def _glib_compile_resources_impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.out)
    xml_file = ctx.file.xml
    args = ctx.actions.args()
    if ctx.attr.mode == "header":
        args.add("--generate-header")
    else:
        args.add("--generate-source")
    args.add("--sourcedir=" + ctx.files.resources[0].dirname)
    args.add("--target=" + out.path)
    if ctx.attr.internal:
        args.add("--internal")
    if ctx.attr.c_prefix:
        args.add("--c-name=" + ctx.attr.c_prefix)
    args.add(xml_file.path)

    ctx.actions.run(
        executable = ctx.executable._compiler,
        arguments = [args],
        inputs = [xml_file] + ctx.files.resources,
        outputs = [out],
        mnemonic = "GlibCompileResources",
        progress_message = "Compiling GResource %s (%s)" % (xml_file.short_path, ctx.attr.out),
    )
    return [DefaultInfo(files = depset([out]))]

glib_compile_resources = rule(
    implementation = _glib_compile_resources_impl,
    attrs = {
        "c_prefix": attr.string(mandatory = True),
        "internal": attr.bool(default = True),
        "mode": attr.string(default = "source"),
        "out": attr.string(mandatory = True),
        "resources": attr.label_list(mandatory = True, allow_files = True),
        "xml": attr.label(mandatory = True, allow_single_file = [".xml"]),
        "_compiler": attr.label(
            default = Label("@glib//:glib-compile-resources"),
            executable = True,
            cfg = "exec",
        ),
    },
)
