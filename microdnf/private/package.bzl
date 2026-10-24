"""RPM package rule used by repository rule."""

load("//microdnf/private:providers.bzl", "RpmInfo")

def _rpm_package_impl(ctx):
    direct_files = [ctx.file.rpm_file] if ctx.file.rpm_file else []
    trans_files = [dep[RpmInfo].transitive_rpms for dep in ctx.attr.deps]
    trans_pkgs = [dep[RpmInfo].transitive_packages for dep in ctx.attr.deps]

    return [
        DefaultInfo(files = depset(direct_files)),
        RpmInfo(
            name = ctx.attr.package_name,
            version = ctx.attr.version,
            arch = ctx.attr.arch,
            rpm_file = ctx.file.rpm_file,
            transitive_rpms = depset(
                direct = direct_files,
                transitive = trans_files,
                order = "postorder",
            ),
            transitive_packages = depset(
                direct = [ctx.attr.package_name],
                transitive = trans_pkgs,
            ),
        ),
    ]

rpm_package = rule(
    implementation = _rpm_package_impl,
    doc = "Defines a single RPM package with its metadata, architecture, and dependencies.",
    attrs = {
        "package_name": attr.string(mandatory = True),
        "version": attr.string(mandatory = True),
        "arch": attr.string(mandatory = True),
        "rpm_file": attr.label(mandatory = True, allow_single_file = True),
        "deps": attr.label_list(providers = [RpmInfo], default = []),
    },
)

def _rpm_package_group_impl(ctx):
    trans_rpms = [dep[RpmInfo].transitive_rpms for dep in ctx.attr.deps]
    trans_pkgs = [dep[RpmInfo].transitive_packages for dep in ctx.attr.deps]
    return [
        DefaultInfo(files = depset(transitive = trans_rpms)),
        RpmInfo(
            name = ctx.attr.package_name,
            version = "",
            arch = ctx.attr.arch,
            rpm_file = None,
            transitive_rpms = depset(transitive = trans_rpms, order = "postorder"),
            transitive_packages = depset(transitive = trans_pkgs),
        ),
    ]

rpm_package_group = rule(
    implementation = _rpm_package_group_impl,
    doc = "Aggregates RPM packages without an RPM of its own; with no deps it is an empty RpmInfo.",
    attrs = {
        "package_name": attr.string(default = ""),
        "arch": attr.string(default = ""),
        "deps": attr.label_list(providers = [RpmInfo], default = []),
    },
)
