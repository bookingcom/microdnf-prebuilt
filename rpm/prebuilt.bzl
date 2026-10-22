"""Repository rule to unpack prebuilt microdnf tools archive."""

def _microdnf_tools_repo_impl(repo_ctx):
    archive = repo_ctx.attr.archive
    if archive:
        repo_ctx.extract(archive)

    build_content = """package(default_visibility = ["//visibility:public"])

exports_files(["lock", "rpm2archive", "microdnf"])
"""
    repo_ctx.file("BUILD.bazel", build_content)

microdnf_tools_repo = repository_rule(
    implementation = _microdnf_tools_repo_impl,
    doc = "Extracts prebuilt microdnf tools archive (lock, rpm2archive, microdnf) for downstream workspaces.",
    attrs = {
        "archive": attr.label(
            mandatory = True,
            allow_single_file = True,
            doc = "Prebuilt tools archive file (e.g. microdnf_tools.tar.gz).",
        ),
    },
)
