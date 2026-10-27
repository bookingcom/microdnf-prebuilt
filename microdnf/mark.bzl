"""Public entrypoint for rpm_mark rule."""

load("//microdnf/private:mark.bzl", _rpm_mark = "rpm_mark")

rpm_mark = _rpm_mark
