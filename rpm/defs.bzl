"""Public RPM rules and macros."""

load("//rpm:archive.bzl", _rpm_archive = "rpm_archive")
load("//rpm:extract.bzl", _rpm_extract = "rpm_extract")
load("//rpm:lock.bzl", _rpm_lock = "rpm_lock")
load("//rpm:package.bzl", _rpm_package = "rpm_package")

rpm_archive = _rpm_archive
rpm_extract = _rpm_extract
rpm_lock = _rpm_lock
rpm_package = _rpm_package
