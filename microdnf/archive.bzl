"""Public API for creating archives out of RPM targets."""

load("//microdnf/private:archive.bzl", _rpm_archive = "rpm_archive")

rpm_archive = _rpm_archive
