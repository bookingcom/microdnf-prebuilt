"""Public providers for microdnf rules."""

load(
    "//microdnf/private:providers.bzl",
    _RpmLockFileInfo = "RpmLockFileInfo",
)

RpmLockFileInfo = _RpmLockFileInfo
