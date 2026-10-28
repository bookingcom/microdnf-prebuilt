"""Public bzlmod extension entrypoint for microdnf-prebuilt."""

load(
    "//microdnf/private:extensions.bzl",
    _dnf5_host_alias_repo = "dnf5_host_alias_repo",
    _microdnf_host_alias_repo = "microdnf_host_alias_repo",
    _microdnf_prebuilt_extension = "microdnf_prebuilt_extension",
    _rpm = "rpm",
    _rpm2archive_host_alias_repo = "rpm2archive_host_alias_repo",
    _rpm2files_host_alias_repo = "rpm2files_host_alias_repo",
    _tools = "tools",
)

microdnf_prebuilt_extension = _microdnf_prebuilt_extension
microdnf_host_alias_repo = _microdnf_host_alias_repo
rpm2archive_host_alias_repo = _rpm2archive_host_alias_repo
rpm2files_host_alias_repo = _rpm2files_host_alias_repo
dnf5_host_alias_repo = _dnf5_host_alias_repo
tools = _tools
rpm = _rpm
