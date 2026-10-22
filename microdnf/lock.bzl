"""Public API for generating RPM lockfiles."""

load("//microdnf/private:lock.bzl", _rpm_lock = "rpm_lock")

rpm_lock = _rpm_lock
