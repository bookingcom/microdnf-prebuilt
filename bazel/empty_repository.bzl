"""A repository with an empty root package, to stand in for a repo we do not want fetched."""

def _empty_repository_impl(rctx):
    rctx.file("BUILD.bazel", "")

empty_repository = repository_rule(implementation = _empty_repository_impl)
