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

_TRANSACTION_RESERVED = "rpm_repository '%s': a %s package is named 'transaction', which is reserved for the target aggregating the whole lock"

def _rpm_path(arch, nevra):
    """Path of a package's downloaded RPM inside the repository."""
    return "pkgs/%s/%s.rpm" % (arch, nevra)

def _target(kind, name, attrs, visibility = "private"):
    """BUILD lines for one target; `attrs` is a list of (key, already-rendered value)."""
    lines = [kind + "(", '    name = "%s",' % name]
    for key, value in attrs:
        lines.append("    %s = %s," % (key, value))
    lines.append('    visibility = ["//visibility:%s"],' % visibility)
    lines.append(")")
    lines.append("")
    return lines

def rpm_repository_build(repo_name, platforms_data, all_requested, is_multi_platform):
    """Renders the BUILD.bazel of an rpm_repository from decoded lock data (no I/O, no fail()).

    Args:
      repo_name: repository name, for messages.
      platforms_data: {arch: {"requested": [names], "packages": {name: package dict}}}.
      all_requested: {requested name: True} across all platforms.
      is_multi_platform: whether more than one lock file was given (aliases become selects).

    Returns:
      struct(error, content): `error` is a message (and content None) when the lock is invalid.
    """
    build_lines = [
        'load("@microdnf_prebuilt//microdnf:package.bzl", "rpm_package", "rpm_package_group")',
        "",
        'package(default_visibility = ["//visibility:private"])',
        "",
    ]

    for arch in platforms_data.keys():
        build_lines.append('config_setting(name = "is_%s", constraint_values = ["@platforms//cpu:%s"])' % (arch, arch))
    build_lines.append("")

    if "transaction" in all_requested:
        return struct(error = _TRANSACTION_RESERVED % (repo_name, "requested"), content = None)
    for pdata in platforms_data.values():
        if "transaction" in pdata["packages"]:
            return struct(error = _TRANSACTION_RESERVED % (repo_name, "locked"), content = None)

    for arch, pdata in platforms_data.items():
        pkgs = pdata["packages"]
        for name, p in pkgs.items():
            nevra = p.get("nevra", name)

            # Hard and weak edges, deduplicated in lock order.
            dep_labels = []
            for d in p.get("dependencies", []) + p.get("weak_dependencies", []):
                label = ":%s_%s" % (d, arch)
                if d in pkgs and label not in dep_labels:
                    dep_labels.append(label)

            build_lines.extend(_target("rpm_package", "%s_%s" % (name, arch), [
                ("package_name", '"%s"' % name),
                ("version", '"%s"' % p.get("version", "")),
                ("arch", '"%s"' % p.get("arch", arch)),
                ("rpm_file", '"%s"' % _rpm_path(arch, nevra)),
                ("deps", repr(dep_labels)),
            ]))

    # Aggregate of every package in the lock, including roots pulled only by
    # the installed set. Empty requests get an RPM-less group per arch.
    for arch, pdata in platforms_data.items():
        pkgs = pdata["packages"]
        build_lines.extend(_target("rpm_package_group", "transaction_%s" % arch, [
            ("arch", '"%s"' % arch),
            ("deps", repr([":%s_%s" % (n, arch) for n in pkgs.keys()])),
        ]))
        for req in all_requested.keys():
            if req not in pkgs:
                # Satisfied by the installroot (or absent on this arch): no RPMs.
                build_lines.extend(_target("rpm_package_group", "%s_%s" % (req, arch), [
                    ("arch", '"%s"' % arch),
                ]))

    # Public aliases for requested packages
    build_lines.append("# Public proxy targets for requested RPM packages")
    arch_list = list(platforms_data.keys())

    for req in list(all_requested.keys()) + ["transaction"]:
        if not is_multi_platform:
            actual = ['    actual = ":%s_%s",' % (req, arch_list[0])]
        else:
            actual = ["    actual = select({"]
            actual += ['        ":is_%s": ":%s_%s",' % (arch, req, arch) for arch in arch_list]
            actual.append("    }),")
        build_lines.append("alias(")
        build_lines.append('    name = "%s",' % req)
        build_lines.extend(actual)
        build_lines.append('    visibility = ["//visibility:public"],')
        build_lines.append(")")
        build_lines.append("")
    return struct(error = None, content = "\n".join(build_lines))

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
                rpm_filename = _rpm_path(norm_arch, nevra)

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

    build = rpm_repository_build(repo_ctx.name, platforms_data, all_requested, is_multi_platform)
    if build.error:
        fail(build.error)
    repo_ctx.file("BUILD.bazel", build.content)

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
