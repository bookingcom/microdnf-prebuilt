"""Public API for extracting files from RPM archives."""

load("//microdnf/private:extract.bzl", _rpm_extract = "rpm_extract")

rpm_extract = _rpm_extract
