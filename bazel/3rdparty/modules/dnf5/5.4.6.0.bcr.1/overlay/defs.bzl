"""Helpers for the dnf5 BUILD file."""

def domain(name):
    """The gettext domain define the bgettext macros require."""
    return ["-DGETTEXT_DOMAIN=\\\"%s\\\"" % name]

def entry_points(base, prefix):
    """Gives a plugin's extern "C" entry points a unique prefix.

    Every plugin exports the same entry points; renamed per plugin they can coexist in one binary.
    dnf5/main.cpp and libdnf5/plugin/plugins.cpp call them by the prefixed names.
    """
    return [
        "-D{b}_{e}={p}_{b}_{e}".format(b = base, e = e, p = prefix)
        for e in ["get_api_version", "get_name", "get_version", "new_instance", "delete_instance", "get_last_exception"]
    ]
