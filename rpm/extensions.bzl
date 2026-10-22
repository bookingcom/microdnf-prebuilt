"""Module extension for declaring proxy RPM repositories and prebuilt tools."""

load("//rpm:prebuilt.bzl", "microdnf_tools_repo")
load("//rpm:repository.bzl", "rpm_repository")

_repository_tag = tag_class(
    attrs = {
        "name": attr.string(mandatory = True),
        "lock_file": attr.label(allow_single_file = [".json"]),
        "lock_files": attr.label_list(allow_files = [".json"]),
    },
)

_prebuilt_tag = tag_class(
    attrs = {
        "name": attr.string(default = "microdnf_tools"),
        "archive": attr.label(mandatory = True, allow_single_file = True),
    },
)

def _rpm_extension_impl(module_ctx):
    for mod in module_ctx.modules:
        for repo_tag in mod.tags.repository:
            lock_files = []
            if repo_tag.lock_file:
                lock_files.append(repo_tag.lock_file)
            if repo_tag.lock_files:
                for lf in repo_tag.lock_files:
                    if lf not in lock_files:
                        lock_files.append(lf)

            rpm_repository(
                name = repo_tag.name,
                lock_files = lock_files,
            )

        for p_tag in mod.tags.prebuilt:
            microdnf_tools_repo(
                name = p_tag.name,
                archive = p_tag.archive,
            )

rpm = module_extension(
    implementation = _rpm_extension_impl,
    tag_classes = {
        "prebuilt": _prebuilt_tag,
        "repository": _repository_tag,
    },
)

def _tools_extension_impl(module_ctx):
    for mod in module_ctx.modules:
        for p_tag in mod.tags.prebuilt:
            microdnf_tools_repo(
                name = p_tag.name,
                archive = p_tag.archive,
            )

tools = module_extension(
    implementation = _tools_extension_impl,
    tag_classes = {
        "prebuilt": _prebuilt_tag,
    },
)
