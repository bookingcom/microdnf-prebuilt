"""Public definitions entrypoint for microdnf."""

load(
    "//microdnf/private:extensions.bzl",
    _dnf_lock_host_alias_repo = "dnf_lock_host_alias_repo",
    _microdnf_host_alias_repo = "microdnf_host_alias_repo",
    _microdnf_prebuilt_extension = "microdnf_prebuilt_extension",
    _rpm2archive_host_alias_repo = "rpm2archive_host_alias_repo",
)
load(
    "//microdnf/private:toolchain.bzl",
    _MicrodnfInfo = "MicrodnfInfo",
    _microdnf_toolchain = "microdnf_toolchain",
)

MicrodnfInfo = _MicrodnfInfo
microdnf_toolchain = _microdnf_toolchain
microdnf_prebuilt_extension = _microdnf_prebuilt_extension
microdnf_host_alias_repo = _microdnf_host_alias_repo
rpm2archive_host_alias_repo = _rpm2archive_host_alias_repo
dnf_lock_host_alias_repo = _dnf_lock_host_alias_repo
