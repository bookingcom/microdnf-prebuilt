"""Provider definitions for microdnf rules."""

RpmLockFileInfo = provider(
    doc = "Propagates generated RPM lockfile details and dependency closure.",
    fields = {
        "arch": "Target architecture(s) for the lockfile",
        "lock_file": "File object representing the output lock JSON file",
        "packages": "List of package names included in the lockfile",
    },
)
