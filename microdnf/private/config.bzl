"""Implementation of the dnf_config rule: a hermetic dnf5 configuration and the command line preamble that pins it."""

load("//microdnf/private:providers.bzl", "DnfConfigInfo")

# Path tokens in DNF5_PREAMBLE. The consuming action supplies them at run time, since only it knows the absolute
# paths of its sandbox:
#   @CONFDIR@  directory holding the rendered dnf.conf and repos.d/ (the directory of the rule's output)
#   @TMP@      scratch directory of the action, writable and empty at the start
#   @REPODIR@  directory holding the local repository's repodata/ (for repo files with an @REPO_DIR@ placeholder)
TOKEN_CONFDIR = "@CONFDIR@"
TOKEN_TMP = "@TMP@"
TOKEN_REPODIR = "@REPODIR@"

# The fixed preamble of every dnf5 action. It is the only place that decides what dnf5 may read and write, and users of
# the rules cannot override it. A flag that is absent keeps its safe default, and is noted so.
DNF5_PREAMBLE = [
    # Our generated dnf.conf, used as is: without it dnf5 reads <installroot>/etc/dnf/dnf.conf and its drop-ins.
    "--config=" + TOKEN_CONFDIR + "/dnf.conf",
    # An empty installroot, so that no host rpmdb, vendor policy or drop-in is seen. dnf5 adds it to most paths below.
    "--installroot=" + TOKEN_TMP + "/root",
    # Repositories come from our .repo files only. On the command line because the installroot would otherwise be
    # prefixed to reposdir from dnf.conf (INSTALLROOT priority beats MAINCONFIG).
    "--setopt=reposdir=" + TOKEN_CONFDIR + "/repos.d",
    # Metadata cache, otherwise ~/.cache/libdnf5 for non-root users.
    "--setopt=cachedir=" + TOKEN_TMP + "/cache",
    # dnf5.log, otherwise ~/.local/state/dnf5 for non-root users.
    "--setopt=logdir=" + TOKEN_TMP + "/log",
    # Persistent data, otherwise /var/lib/dnf.
    "--setopt=persistdir=" + TOKEN_TMP + "/persist",
    # Module and system state, and the transaction history, otherwise /usr/lib/sysimage/libdnf5.
    "--setopt=system_state_dir=" + TOKEN_TMP + "/state",
    "--setopt=transaction_history_dir=" + TOKEN_TMP + "/history",
    # Solver debug dumps, otherwise ./debugdata.
    "--setopt=debugdir=" + TOKEN_TMP + "/debug",
    # No variable directories (/etc/dnf/vars, /usr/share/dnf5/vars.d); the repo directory is passed as a variable below.
    "--setopt=varsdir=",
    # libdnf5 plugins are off in dnf.conf; their configuration and module directories point at paths that do not exist.
    "--setopt=pluginconfpath=" + TOKEN_TMP + "/plugins.d",
    "--setopt=plugin_conf_dir=" + TOKEN_TMP + "/plugins.d",
    "--setopt=pluginpath=" + TOKEN_TMP + "/plugins",
    # Expands ${repo_dir} in the rendered .repo files.
    "--setvar=repo_dir=" + TOKEN_REPODIR,
    # Not passed: --use-host-config (off by default, keeps configuration and vars inside the installroot).
]

def _repo_dir(repo_files):
    """Execroot-relative directory that holds repodata/, derived like rpm_lock does."""
    for f in repo_files:
        if "/repodata/" in f.path or f.path.endswith("/repodata"):
            return f.path.split("/repodata")[0]
    return repo_files[0].dirname if repo_files else ""

def _dnf_config_impl(ctx):
    main = {
        "best": "False" if ctx.attr.nobest else "True",
        "install_weak_deps": "True" if ctx.attr.install_weak_deps else "False",
        "plugins": "False",
        # An empty value resets the list, whose default holds a glob of /etc/dnf/protected.d/*.conf.
        "protected_packages": "",
        # The default appends the host's /etc/os-release name and arch.
        "user_agent": "libdnf",
    }
    if ctx.attr.excludepkgs:
        main["excludepkgs"] = ",".join(ctx.attr.excludepkgs)
    if ctx.attr.module_platform_id:
        main["module_platform_id"] = ctx.attr.module_platform_id

    conf = ctx.actions.declare_file(ctx.label.name + "/dnf.conf")
    ctx.actions.write(conf, "[main]\n" + "".join(["%s=%s\n" % (k, main[k]) for k in sorted(main)]))

    repos = []
    seen = {}
    for template in ctx.files.repo_configs:
        stem = template.basename.rsplit(".", 1)[0]
        if stem in seen:
            fail("repo_configs: two files named %s.*" % stem)
        seen[stem] = True
        repo = ctx.actions.declare_file("%s/repos.d/%s.repo" % (ctx.label.name, stem))
        ctx.actions.expand_template(
            template = template,
            output = repo,
            substitutions = {"@REPO_DIR@": "${repo_dir}"},
        )
        repos.append(repo)

    forcearch = ctx.attr.forcearch
    if not forcearch:
        forcearch = ([a for a in ctx.attr.arch if a != "noarch"] or [""])[0]
    preamble = list(DNF5_PREAMBLE) + ["--releasever=" + ctx.attr.releasever]
    if forcearch:
        preamble.append("--forcearch=" + forcearch)

    args_file = ctx.actions.declare_file(ctx.label.name + "/preamble.args")
    ctx.actions.write(args_file, "".join([a + "\n" for a in preamble]))

    repo_files = ctx.files.repo_files
    data = [conf, args_file] + repos + repo_files
    return [
        DefaultInfo(
            files = depset([conf]),
            runfiles = ctx.runfiles(files = data),
        ),
        DnfConfigInfo(
            arch = ctx.attr.arch,
            args_file = args_file,
            conf = conf,
            files = depset(data),
            forcearch = forcearch,
            module_enable = ctx.attr.module_enable,
            preamble = preamble,
            releasever = ctx.attr.releasever,
            repo_dir = _repo_dir(repo_files),
            repos = repos,
        ),
    ]

dnf_config = rule(
    implementation = _dnf_config_impl,
    doc = """Renders a dnf5 configuration: dnf.conf (main section) and one .repo file per repository.

The provider carries the files and DNF5_PREAMBLE, the fixed command line that makes dnf5 read only them.""",
    attrs = {
        "arch": attr.string_list(
            default = ["x86_64", "noarch"],
            doc = "Target CPU architectures, as for rpm_lock. forcearch defaults to the first one that is not noarch.",
        ),
        "excludepkgs": attr.string_list(
            doc = "NEVRA globs hidden from the solver (excludepkgs=).",
        ),
        "forcearch": attr.string(
            doc = "Architecture to solve for instead of the host's (--forcearch).",
        ),
        "install_weak_deps": attr.bool(
            default = True,
            doc = "Install weak dependencies (install_weak_deps=).",
        ),
        "module_enable": attr.string_list(
            doc = "Module streams (NAME:STREAM) to enable before resolving. dnf5 keeps module state in system_state_dir, " +
                  "not in dnf.conf, so the consuming action runs `module enable` for them; the rule only carries the list.",
        ),
        "module_platform_id": attr.string(
            doc = "Platform module (e.g. 'platform:el9') that module streams are resolved against (module_platform_id=).",
        ),
        "nobest": attr.bool(
            doc = "Allow older candidates when the newest requested package is not installable (best=False).",
        ),
        "releasever": attr.string(
            mandatory = True,
            doc = "Distribution release version (--releasever).",
        ),
        "repo_configs": attr.label_list(
            mandatory = True,
            allow_empty = False,
            allow_files = [".repo", ".conf"],
            doc = "Repository definitions, one file per repository, as rpm_lock's repo_config. " +
                  "A placeholder @REPO_DIR@ stands for the directory of repo_files.",
        ),
        "repo_files": attr.label_list(
            allow_files = True,
            doc = "Local repository metadata and packages (repodata/...), as rpm_lock's repo_files.",
        ),
    },
)
