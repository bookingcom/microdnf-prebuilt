"""An rpm_lock case with its normalized lockfile, for the golden files in //e2e/testdata."""

load("@bazel_lib//lib:run_binary.bzl", "run_binary")
load("//microdnf:defs.bzl", "rpm_lock")

def lock_golden(name, out, **kwargs):
    """Declares an rpm_lock <name> and <name>_norm.json, its lockfile with the sandbox paths replaced.

    //e2e/testdata:lock_golden diffs <name>_norm.json against the checked-in golden file of the case.

    Args:
      name: name of the rpm_lock target.
      out: lockfile name of the rpm_lock target.
      **kwargs: rpm_lock attributes.
    """
    rpm_lock(name = name, out = out, **kwargs)
    run_binary(
        name = name + "_norm",
        srcs = [out],
        outs = [name + "_norm.json"],
        args = ["$(location %s)" % out, "$(location %s_norm.json)" % name],
        tool = "//e2e:normalize_lock",
    )
