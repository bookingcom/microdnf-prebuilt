"""Provider definitions for microdnf rules."""

RpmLockFileInfo = provider(
    doc = "Propagates generated RPM lockfile details and dependency closure.",
    fields = {
        "arch": "Target architecture(s) for the lockfile",
        "lock_file": "File object representing the output lock JSON file",
        "packages": "List of package names included in the lockfile",
    },
)

RpmInfo = provider(
    doc = "Propagates RPM target metadata, architecture, and files.",
    fields = {
        "arch": "Target architecture of the RPM package",
        "name": "Package name",
        "rpm_file": "File object representing the .rpm file",
        "transitive_packages": "Depset of transitive package names",
        "transitive_rpms": "Depset of transitive RPM files including this target",
        "version": "Package version",
    },
)
