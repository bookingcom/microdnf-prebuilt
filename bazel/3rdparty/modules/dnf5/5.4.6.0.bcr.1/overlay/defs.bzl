"""Helpers for the dnf5 BUILD file."""

def domain(name):
    """The gettext domain define the bgettext macros require."""
    return ["-DGETTEXT_DOMAIN=\\\"%s\\\"" % name]
