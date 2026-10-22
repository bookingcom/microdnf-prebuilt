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

RpmArchiveInfo = provider(
    doc = "Propagates generated archive file and architecture from rpm_archive.",
    fields = {
        "arch": "Resolved target architecture of the archive",
        "archive": "Output archive File object (.tar, .tar.gz, .tar.zst)",
        "rpms": "Depset of input RPM files",
    },
)

RpmExtractedFilesInfo = provider(
    doc = "Propagates files extracted from an rpm_archive and executable targets.",
    fields = {
        "arch": "Architecture of the extracted files",
        "executables": "Depset of executable File targets with 0755 permissions",
        "files": "Depset of all extracted File objects",
    },
)
