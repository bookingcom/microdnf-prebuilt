"""Public repository rule for proxy RPM repositories."""

load("//microdnf/private:repository.bzl", _rpm_repository = "rpm_repository")

rpm_repository = _rpm_repository
