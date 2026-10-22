"""Public providers for microdnf rules."""

load(
    "//microdnf/private:providers.bzl",
    _RpmInfo = "RpmInfo",
    _RpmLockFileInfo = "RpmLockFileInfo",
)

RpmLockFileInfo = _RpmLockFileInfo
RpmInfo = _RpmInfo
