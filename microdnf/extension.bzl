"""Alias for //microdnf:extensions.bzl."""

load(
    "//microdnf:extensions.bzl",
    _dnf_lock_host_alias_repo = "dnf_lock_host_alias_repo",
    _microdnf_host_alias_repo = "microdnf_host_alias_repo",
    _microdnf_prebuilt_extension = "microdnf_prebuilt_extension",
    _rpm2archive_host_alias_repo = "rpm2archive_host_alias_repo",
)

microdnf_prebuilt_extension = _microdnf_prebuilt_extension
microdnf_host_alias_repo = _microdnf_host_alias_repo
rpm2archive_host_alias_repo = _rpm2archive_host_alias_repo
dnf_lock_host_alias_repo = _dnf_lock_host_alias_repo
