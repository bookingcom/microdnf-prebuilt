"""Unit tests for rpm_repository BUILD generation (the reserved 'transaction' name)."""

load("@bazel_skylib//lib:unittest.bzl", "asserts", "unittest")
load("//microdnf/private:repository.bzl", "rpm_repository_build")

_PKG = {"nevra": "foo-1-1.x86_64", "version": "1", "arch": "x86_64"}

def _platform(requested, names):
    return {"requested": requested, "packages": {n: dict(_PKG, name = n) for n in names}}

_RESERVED = "a %s package is named 'transaction', which is reserved for the target aggregating the whole lock"

def _requested_transaction_test_impl(ctx):
    env = unittest.begin(ctx)
    result = rpm_repository_build("r", {"x86_64": _platform(["transaction"], [])}, {"transaction": True}, False)
    asserts.equals(env, "rpm_repository 'r': " + _RESERVED % "requested", result.error)
    asserts.equals(env, None, result.content)
    return unittest.end(env)

def _locked_transaction_test_impl(ctx):
    env = unittest.begin(ctx)
    platforms = {
        "x86_64": _platform(["foo"], ["foo"]),
        "aarch64": _platform(["foo"], ["foo", "transaction"]),
    }
    result = rpm_repository_build("r", platforms, {"foo": True}, True)
    asserts.equals(env, "rpm_repository 'r': " + _RESERVED % "locked", result.error)
    asserts.equals(env, None, result.content)
    return unittest.end(env)

def _clean_lock_test_impl(ctx):
    env = unittest.begin(ctx)
    platforms = {
        "x86_64": _platform(["foo"], ["foo"]),
        "aarch64": _platform(["foo"], ["foo"]),
    }
    result = rpm_repository_build("r", platforms, {"foo": True}, True)
    asserts.equals(env, None, result.error)
    asserts.true(env, 'name = "foo_x86_64",' in result.content)
    asserts.true(env, 'name = "transaction_aarch64",' in result.content)
    asserts.true(env, 'name = "transaction",' in result.content)
    return unittest.end(env)

requested_transaction_test = unittest.make(_requested_transaction_test_impl)
locked_transaction_test = unittest.make(_locked_transaction_test_impl)
clean_lock_test = unittest.make(_clean_lock_test_impl)

def repository_test_suite(name):
    unittest.suite(
        name,
        requested_transaction_test,
        locked_transaction_test,
        clean_lock_test,
    )
