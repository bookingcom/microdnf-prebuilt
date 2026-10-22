"""Repository rule creating proxy RPM repositories from JSON lockfiles."""

def _normalize_arch(raw_arch):
    arch = raw_arch.lower().strip()
    if arch in ["x86_64", "amd64"]:
        return "x86_64"
    if arch in ["aarch64", "arm64"]:
        return "aarch64"
    if arch in ["noarch"]:
        return "noarch"
    return arch

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

            # Download or locate local RPM payload
            url = p.get("url", "")
            if url:
                nevra = p.get("nevra", name)
                rpm_filename = "pkgs/%s/%s.rpm" % (norm_arch, nevra)

                integrity = p.get("integrity", "")
                sha256 = p.get("checksum", "")

                if not (url.startswith("http://") or url.startswith("https://")):
                    local_path = url[7:] if url.startswith("file://") else url
                    fname = local_path.split("/")[-1]
                    lf_path = repo_ctx.path(lf)
                    lf_dir = str(lf_path.dirname)

                    # Look near lockfile first, then try the direct path in workspace
                    found_path = None
                    for candidate in [
                        "%s/%s" % (lf_dir, fname),
                        "%s/repo/%s" % (lf_dir, fname),
                        "%s/repo/disagree/%s" % (lf_dir, fname),
                    ]:
                        cand = repo_ctx.path(candidate)
                        if cand.exists:
                            found_path = cand
                            break

                    if not found_path and repo_ctx.path(local_path).exists:
                        found_path = repo_ctx.path(local_path)

                    if found_path:
                        url = "file://" + str(found_path)

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
        'load("@microdnf_prebuilt//microdnf:package.bzl", "rpm_package")',
        "",
        'package(default_visibility = ["//visibility:private"])',
        "",
    ]

    for arch in platforms_data.keys():
        build_lines.append('config_setting(name = "is_%s", constraint_values = ["@platforms//cpu:%s"])' % (arch, arch))
    build_lines.append("")

    for arch, pdata in platforms_data.items():
        pkgs = pdata["packages"]
        for name, p in pkgs.items():
            nevra = p.get("nevra", name)
            deps = p.get("dependencies", [])
            dep_labels = [":%s_%s" % (d, arch) for d in deps if d in pkgs]

            build_lines.append("rpm_package(")
            build_lines.append('    name = "%s_%s",' % (name, arch))
            build_lines.append('    package_name = "%s",' % name)
            build_lines.append('    version = "%s",' % p.get("version", ""))
            build_lines.append('    arch = "%s",' % p.get("arch", arch))
            build_lines.append('    rpm_file = "pkgs/%s/%s.rpm",' % (arch, nevra))
            build_lines.append("    deps = %s," % repr(dep_labels))
            build_lines.append('    visibility = ["//visibility:private"],')
            build_lines.append(")")
            build_lines.append("")

    # Public aliases for requested packages
    build_lines.append("# Public proxy targets for requested RPM packages")
    arch_list = list(platforms_data.keys())

    for req in all_requested.keys():
        if not is_multi_platform:
            single_arch = arch_list[0]
            build_lines.append("alias(")
            build_lines.append('    name = "%s",' % req)
            build_lines.append('    actual = ":%s_%s",' % (req, single_arch))
            build_lines.append('    visibility = ["//visibility:public"],')
            build_lines.append(")")
            build_lines.append("")
        else:
            select_dict = {}
            for arch in arch_list:
                if req in platforms_data[arch]["packages"]:
                    select_dict[":is_%s" % arch] = ":%s_%s" % (req, arch)

            select_entries = []
            for k, v in select_dict.items():
                select_entries.append('        "%s": "%s",' % (k, v))

            build_lines.append("alias(")
            build_lines.append('    name = "%s",' % req)
            build_lines.append("    actual = select({")
            build_lines.extend(select_entries)
            build_lines.append("    }),")
            build_lines.append('    visibility = ["//visibility:public"],')
            build_lines.append(")")
            build_lines.append("")

    repo_ctx.file("BUILD.bazel", "\n".join(build_lines))

rpm_repository = repository_rule(
    implementation = _rpm_repository_impl,
    doc = "Creates a proxy Bazel repository exposing only the requested RPM targets from lockfiles.",
    attrs = {
        "lock_file": attr.label(
            allow_single_file = [".json"],
            doc = "Single platform lock file.",
        ),
        "lock_files": attr.label_list(
            allow_files = [".json"],
            doc = "List of lock files for multi-platform repository generation.",
        ),
    },
)
