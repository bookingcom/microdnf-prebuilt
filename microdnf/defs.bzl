"""Public definitions entrypoint for microdnf."""

load(
    "//microdnf/private:archive.bzl",
    _rpm_archive = "rpm_archive",
)
load(
    "//microdnf/private:extensions.bzl",
    _dnf_lock_host_alias_repo = "dnf_lock_host_alias_repo",
    _microdnf_host_alias_repo = "microdnf_host_alias_repo",
    _microdnf_prebuilt_extension = "microdnf_prebuilt_extension",
    _rpm2archive_host_alias_repo = "rpm2archive_host_alias_repo",
)
load(
    "//microdnf/private:extract.bzl",
    _rpm_extract = "rpm_extract",
)
load(
    "//microdnf/private:lock.bzl",
    _rpm_lock = "rpm_lock",
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

rpm_lock = _rpm_lock
rpm_archive = _rpm_archive
rpm_extract = _rpm_extract
