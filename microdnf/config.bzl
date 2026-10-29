"""Public API for generating a hermetic dnf5 configuration."""

load("//microdnf/private:config.bzl", _DNF5_PREAMBLE = "DNF5_PREAMBLE", _dnf_config = "dnf_config")
load("//microdnf/private:providers.bzl", _DnfConfigInfo = "DnfConfigInfo")

dnf_config = _dnf_config
DnfConfigInfo = _DnfConfigInfo
DNF5_PREAMBLE = _DNF5_PREAMBLE
