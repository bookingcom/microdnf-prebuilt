"""Test helper writing the transitive RpmInfo of a target to a text file."""

load("//microdnf/private:providers.bzl", "RpmInfo")

def _rpm_listing_impl(ctx):
    info = ctx.attr.rpm[RpmInfo]
    out = ctx.actions.declare_file(ctx.label.name + ".txt")
    lines = ["package " + n for n in sorted(info.transitive_packages.to_list())]
    lines += ["rpm " + f.basename for f in sorted(info.transitive_rpms.to_list(), key = lambda f: f.basename)]
    lines.append("arch " + info.arch)
    ctx.actions.write(out, "\n".join(lines) + "\n")
    return [DefaultInfo(files = depset([out]))]

rpm_listing = rule(
    implementation = _rpm_listing_impl,
    attrs = {"rpm": attr.label(mandatory = True, providers = [RpmInfo])},
)
