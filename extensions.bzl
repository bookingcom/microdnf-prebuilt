"""Public module extensions for RPM rules and prebuilt tools."""

load("//rpm:extensions.bzl", _rpm = "rpm", _tools = "tools")

rpm = _rpm
tools = _tools
