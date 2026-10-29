"""Provider definitions for microdnf rules."""

RpmLockFileInfo = provider(
    doc = "Propagates generated RPM lockfile details and dependency closure.",
    fields = {
        "arch": "Target architecture(s) for the lockfile",
        "lock_file": "File object representing the output lock JSON file",
        "packages": "List of package names included in the lockfile",
    },
)

DnfConfigInfo = provider(
    doc = "A rendered dnf5 configuration and the command line preamble that pins it.",
    fields = {
        "arch": "Target architectures",
        "args_file": "File with the preamble, one argument per line, path tokens unexpanded",
        "conf": "The rendered dnf.conf; its directory is @CONFDIR@ in the preamble",
        "files": "Depset of everything the preamble refers to (dnf.conf, repos.d, repository files)",
        "forcearch": "Architecture passed as --forcearch, or empty",
        "module_enable": "Module streams (NAME:STREAM) the consuming action enables",
        "preamble": "List of dnf5 arguments with the tokens @CONFDIR@, @TMP@ and @REPODIR@ for the consumer to expand",
        "releasever": "Release version passed as --releasever",
        "repo_dir": "Execroot-relative directory of the local repository, the value of @REPODIR@",
        "repos": "List of rendered .repo files",
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
