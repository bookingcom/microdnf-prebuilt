"""Public entrypoint for RPM packaging rules."""

load("//rpm:lock.bzl", _rpm_lock = "rpm_lock")
load("//rpm:package.bzl", _rpm_package = "rpm_package")

rpm_lock = _rpm_lock
rpm_package = _rpm_package
