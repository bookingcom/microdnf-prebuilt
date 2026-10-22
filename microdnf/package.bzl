"""Public API for declaring RPM package targets."""

load("//microdnf/private:package.bzl", _rpm_package = "rpm_package")

rpm_package = _rpm_package
