"""Public entrypoint for RPM packaging rules."""

load("//rpm:lock.bzl", _rpm_lock = "rpm_lock")

rpm_lock = _rpm_lock
