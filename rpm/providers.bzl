"""RPM rules providers."""

RpmLockFileInfo = provider(
    doc = "Provider for generated RPM lockfile.",
    fields = {
        "lock_file": "File: The JSON lock file.",
        "requested": "list[str]: The directly requested RPM package names.",
        "arch": "str: Target architecture.",
        "releasever": "str: Target OS release version.",
    },
)

RpmInfo = provider(
    doc = "Provider representing a resolved RPM package.",
    fields = {
        "name": "str: Package name.",
        "version": "str: Package EVR.",
        "arch": "str: Package architecture.",
        "rpm_file": "File: The .rpm file.",
        "transitive_rpms": "depset[File]: Depset of all transitive .rpm files in dependency order.",
        "transitive_packages": "depset[str]: Depset of all transitive package names.",
    },
)

RpmArchiveInfo = provider(
    doc = "Provider representing an archive constructed from RPMs.",
    fields = {
        "archive": "File: The output archive file (.tar, .tar.gz, .tar.zst).",
        "arch": "str: Target architecture propagated from the RPMs.",
        "rpms": "depset[File]: Depset of all RPM files included in the archive.",
    },
)

RpmExtractedFilesInfo = provider(
    doc = "Provider representing files extracted from an RPM archive.",
    fields = {
        "files": "depset[File]: All extracted files.",
        "executables": "depset[File]: All extracted files marked executable.",
        "arch": "str: Target architecture propagated from the archive.",
    },
)
