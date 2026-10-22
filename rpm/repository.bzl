"""Repository rule for proxying RPM packages from lock files."""

def _normalize_arch(arch):
    if arch in ["x86_64", "amd64"]:
        return "x86_64"
    if arch in ["aarch64", "arm64"]:
        return "aarch64"
    if arch in ["s390x"]:
        return "s390x"
    if arch in ["ppc64le"]:
        return "ppc64le"
    return arch

def _cpu_constraint_for_arch(arch):
    norm = _normalize_arch(arch)
    if norm == "x86_64":
        return "@platforms//cpu:x86_64"
    if norm == "aarch64":
        return "@platforms//cpu:aarch64"
    if norm == "s390x":
        return "@platforms//cpu:s390x"
    if norm == "ppc64le":
        return "@platforms//cpu:ppc64le"
    return "@platforms//cpu:" + norm

def _rpm_repository_impl(repo_ctx):
    lock_files = []
    if repo_ctx.attr.lock_file:
        lock_files.append(repo_ctx.attr.lock_file)
    for lf in repo_ctx.attr.lock_files:
        if lf not in lock_files:
            lock_files.append(lf)

    if not lock_files:
        fail("rpm_repository '%s' requires either 'lock_file' or 'lock_files'" % repo_ctx.name)

    is_multi_platform = len(lock_files) > 1

    # Map arch -> { "requested": [...], "packages": { name: pkg_dict } }
    platforms_data = {}
    all_requested = {}

    for lf in lock_files:
        content = repo_ctx.read(lf)
        data = json.decode(content)

        lf_arch = data.get("arch", "")
        packages = data.get("packages", [])

        if not lf_arch:
            for p in packages:
                p_arch = p.get("arch", "")
                if p_arch and p_arch != "noarch":
                    lf_arch = p_arch
                    break
        if not lf_arch:
            lf_arch = "noarch"

        norm_arch = _normalize_arch(lf_arch.split(",")[0].strip())

        req_list = data.get("requested", [])
        if not req_list:
            for p in packages:
                if p.get("requested", False):
                    req_list.append(p.get("name"))

        for r in req_list:
            all_requested[r] = True

        pkg_map = {}
        for p in packages:
            name = p.get("name")
            if not name:
                continue
            pkg_map[name] = p

            # Download the RPM payload
            url = p.get("url", "")
            if url:
                nevra = p.get("nevra", name)
                rpm_filename = "pkgs/%s/%s.rpm" % (norm_arch, nevra)

                integrity = p.get("integrity", "")
                sha256 = p.get("checksum", "")

                if url.startswith("file://"):
                    local_path = url[7:]
                    if not repo_ctx.path(local_path).exists:
                        # Fallback: look near lockfile or in repo
                        fname = local_path.split("/")[-1]
                        candidate = "%s/%s" % (lf.dirname, fname)
                        if repo_ctx.path(candidate).exists:
                            url = "file://" + str(repo_ctx.path(candidate))

                download_kwargs = {
                    "url": url,
                    "output": rpm_filename,
                }
                if integrity:
                    download_kwargs["integrity"] = integrity
                elif sha256:
                    download_kwargs["sha256"] = sha256

                repo_ctx.download(**download_kwargs)

        platforms_data[norm_arch] = {
            "requested": req_list,
            "packages": pkg_map,
        }

    # Generate BUILD.bazel
    build_lines = [
        'load("@microdnf_prebuilt//rpm:package.bzl", "rpm_package")',
        "",
        'package(default_visibility = ["//visibility:private"])',
        "",
    ]

    if not is_multi_platform:
        # Single platform
        arch = list(platforms_data.keys())[0]
        pdata = platforms_data[arch]
        pkg_map = pdata["packages"]

        for name, p in pkg_map.items():
            version = p.get("version", "")
            pkg_arch = p.get("arch", arch)
            nevra = p.get("nevra", name)
            deps = p.get("dependencies", [])

            rpm_file = "pkgs/%s/%s.rpm" % (arch, nevra)
            dep_labels = [":_pkg_%s" % d for d in deps if d in pkg_map]

            build_lines.append("rpm_package(")
            build_lines.append('    name = "_pkg_%s",' % name)
            build_lines.append('    package_name = "%s",' % name)
            build_lines.append('    version = "%s",' % version)
            build_lines.append('    arch = "%s",' % pkg_arch)
            build_lines.append('    rpm_file = "%s",' % rpm_file)
            build_lines.append("    deps = [%s]," % ", ".join(['"%s"' % d for d in dep_labels]))
            build_lines.append(")")
            build_lines.append("")

        # Expose ONLY the requested set of RPMs as public aliases
        for r in pdata["requested"]:
            if r in pkg_map:
                build_lines.append("alias(")
                build_lines.append('    name = "%s",' % r)
                build_lines.append('    actual = ":_pkg_%s",' % r)
                build_lines.append('    visibility = ["//visibility:public"],')
                build_lines.append(")")
                build_lines.append("")

    else:
        # Multi-platform: 2 or more lock files
        # Define internal per-arch packages
        for arch, pdata in platforms_data.items():
            pkg_map = pdata["packages"]
            for name, p in pkg_map.items():
                version = p.get("version", "")
                pkg_arch = p.get("arch", arch)
                nevra = p.get("nevra", name)
                deps = p.get("dependencies", [])

                rpm_file = "pkgs/%s/%s.rpm" % (arch, nevra)
                dep_labels = [":_pkg_%s_%s" % (d, arch) for d in deps if d in pkg_map]

                build_lines.append("rpm_package(")
                build_lines.append('    name = "_pkg_%s_%s",' % (name, arch))
                build_lines.append('    package_name = "%s",' % name)
                build_lines.append('    version = "%s",' % version)
                build_lines.append('    arch = "%s",' % pkg_arch)
                build_lines.append('    rpm_file = "%s",' % rpm_file)
                build_lines.append("    deps = [%s]," % ", ".join(['"%s"' % d for d in dep_labels]))
                build_lines.append(")")
                build_lines.append("")

        # For each requested package, expose public alias with select()
        default_arch = list(platforms_data.keys())[0]
        for r in sorted(all_requested.keys()):
            select_dict = {}
            for arch in platforms_data.keys():
                if r in platforms_data[arch]["packages"]:
                    select_dict[_cpu_constraint_for_arch(arch)] = ":_pkg_%s_%s" % (r, arch)

            if select_dict:
                build_lines.append("alias(")
                build_lines.append('    name = "%s",' % r)
                build_lines.append("    actual = select({")
                for constraint, target in select_dict.items():
                    build_lines.append('        "%s": "%s",' % (constraint, target))
                build_lines.append('        "//conditions:default": ":_pkg_%s_%s",' % (r, default_arch))
                build_lines.append("    }),")
                build_lines.append('    visibility = ["//visibility:public"],')
                build_lines.append(")")
                build_lines.append("")

    repo_ctx.file("BUILD.bazel", "\n".join(build_lines))

rpm_repository = repository_rule(
    implementation = _rpm_repository_impl,
    doc = "Creates a proxy Bazel repository exposing only the requested set of RPMs from lockfile(s).",
    attrs = {
        "lock_file": attr.label(
            allow_single_file = [".json"],
            doc = "Single lock file.",
        ),
        "lock_files": attr.label_list(
            allow_files = [".json"],
            doc = "Two or more lock files for multi-platform support.",
        ),
    },
)
