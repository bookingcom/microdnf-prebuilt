"""Parity gate: every libdnf rpm_lock case has a dnf5 twin whose lockfile must be byte-identical."""

load("@bazel_lib//lib:diff_test.bzl", "diff_test")
load("@bazel_lib//lib:run_binary.bzl", "run_binary")
load("//microdnf:defs.bzl", "rpm_lock")

def _normalized(name, lock):
    run_binary(
        name = name,
        srcs = [lock],
        outs = [name + ".json"],
        args = ["$(location %s)" % lock, "$(location %s.json)" % name],
        tool = "//e2e:normalize_lock",
    )

def lock_parity(name, out, **kwargs):
    """Declares <name> (libdnf), <name>_dnf5 and a diff_test of the two lockfiles.

    Args:
      name: name of the libdnf target.
      out: lockfile name of the libdnf target; the twin writes <name>_dnf5.lock.json.
      **kwargs: rpm_lock attributes shared by both targets.
    """
    rpm_lock(name = name, out = out, backend = "libdnf", **kwargs)

    # No backend and dnf5_tool: the twin runs the defaults of rpm_lock, so the parity gate covers them.
    rpm_lock(
        name = name + "_dnf5",
        out = name + "_dnf5.lock.json",
        **kwargs
    )
    _normalized(name + "_norm", out)
    _normalized(name + "_dnf5_norm", name + "_dnf5.lock.json")
    diff_test(
        name = name + "_parity_test",
        file1 = ":" + name + "_norm.json",
        file2 = ":" + name + "_dnf5_norm.json",
        tags = ["lock_parity"],
    )
