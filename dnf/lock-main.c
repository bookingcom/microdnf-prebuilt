/* dnf/lock-main.c
 *
 * Standalone CLI tool to generate JSON lockfiles.
 */

#include "lock.h"
#include "solve-arch.h"
#include <stdio.h>
#include <locale.h>
#include <unistd.h>
#include <glib.h>
#include <rpm/rpmlib.h>
#include <libdnf/libdnf.h>

static GSList *enable_disable_repos = NULL;
static gboolean opt_refresh = FALSE;

static gboolean
process_global_option (const gchar  *option_name,
                       const gchar  *value,
                       gpointer      data,
                       GError      **error)
{
  DnfContext *ctx = DNF_CONTEXT (data);

  if (g_strcmp0 (option_name, "--config") == 0)
    {
      dnf_context_set_config_file_path (value);
    }
  else if (g_strcmp0 (option_name, "--disablerepo") == 0)
    {
      enable_disable_repos = g_slist_append (enable_disable_repos, g_strconcat ("d", value, NULL));
    }
  else if (g_strcmp0 (option_name, "--enablerepo") == 0)
    {
      enable_disable_repos = g_slist_append (enable_disable_repos, g_strconcat ("e", value, NULL));
    }
  else if (g_strcmp0 (option_name, "--installroot") == 0)
    {
      dnf_context_set_install_root (ctx, value);
    }
  else if (g_strcmp0 (option_name, "--releasever") == 0)
    {
      dnf_context_set_release_ver (ctx, value);
    }
  else if (g_strcmp0 (option_name, "--reposdir") == 0)
    {
      g_auto(GStrv) reposdir = g_strsplit (value, ",", -1);
      dnf_context_set_repos_dir (ctx, (const gchar * const *)reposdir);
    }
  else if (g_strcmp0 (option_name, "--setopt") == 0)
    {
      g_auto(GStrv) setopt = g_strsplit (value, "=", 2);
      if (setopt[0] && setopt[1])
        {
          if (strcmp (setopt[0], "cachedir") == 0)
            {
              const char *cachedir = setopt[1];
              g_autofree gchar *metadatadir = g_build_path ("/", cachedir, "metadata", NULL);
              dnf_context_set_cache_dir (ctx, metadatadir);
              g_autofree gchar *solvdir = g_build_path ("/", cachedir, "solv", NULL);
              dnf_context_set_solv_dir (ctx, solvdir);
              g_autofree gchar *lockdir = g_build_path ("/", cachedir, "lock", NULL);
              dnf_context_set_lock_dir (ctx, lockdir);
            }
          else if (strcmp (setopt[0], "reposdir") == 0)
            {
              g_auto(GStrv) reposdir = g_strsplit (setopt[1], ",", -1);
              dnf_context_set_repos_dir (ctx, (const gchar * const *)reposdir);
            }
          else if (strcmp (setopt[0], "varsdir") == 0)
            {
              g_auto(GStrv) varsdir = g_strsplit (setopt[1], ",", -1);
              dnf_context_set_vars_dir (ctx, (const gchar * const *)varsdir);
            }
          else if (strcmp (setopt[0], "module_platform_id") == 0)
            {
              dnf_context_set_platform_module (ctx, setopt[1]);
            }
          else
            {
              /* Main options (excludepkgs, best, ...) and repo_id.option_name
               * options, applied when libdnf loads its config. */
              dnf_conf_add_setopt (setopt[0], DNF_CONF_COMMANDLINE, setopt[1], NULL);
            }
        }
    }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  setlocale (LC_ALL, "");

  /* Hermetic host portability */
  if (access ("/usr/lib/rpm/rpmrc", R_OK) != 0 && !getenv ("RPM_CONFIGDIR"))
    {
      const char *tmp = g_get_tmp_dir ();
      g_autofree char *dir = g_build_filename (tmp, "microdnf_rpm_cfg", NULL);
      g_mkdir_with_parents (dir, 0755);
      g_autofree char *rcfile = g_build_filename (dir, "rpmrc", NULL);
      if (access (rcfile, F_OK) != 0)
        g_file_set_contents (rcfile, "", 0, NULL);
      g_autofree char *macfile = g_build_filename (dir, "macros", NULL);
      if (access (macfile, F_OK) != 0)
        g_file_set_contents (macfile, "", 0, NULL);
      setenv ("RPM_CONFIGDIR", dir, 1);
    }

  g_autoptr(DnfContext) ctx = dnf_context_new ();

#define CACHEDIR "/var/cache/yum"
  if (geteuid () != 0)
    {
      const char *user_cache = g_get_user_cache_dir ();
      g_autofree gchar *metadatadir = g_build_filename (user_cache, "microdnf", "metadata", NULL);
      g_autofree gchar *solvdir = g_build_filename (user_cache, "microdnf", "solv", NULL);
      g_autofree gchar *lockdir = g_build_filename (user_cache, "microdnf", "lock", NULL);
      dnf_context_set_cache_dir (ctx, metadatadir);
      dnf_context_set_solv_dir (ctx, solvdir);
      dnf_context_set_lock_dir (ctx, lockdir);
    }
  else
    {
      dnf_context_set_cache_dir (ctx, CACHEDIR "/metadata");
      dnf_context_set_solv_dir (ctx, CACHEDIR "/solv");
      dnf_context_set_lock_dir (ctx, CACHEDIR "/lock");
    }
#undef CACHEDIR

  dnf_context_set_check_disk_space (ctx, FALSE);
  dnf_context_set_check_transaction (ctx, FALSE);
  dnf_context_set_cache_age (ctx, G_MAXUINT - 1);

  /* Fallback repos directory for non-root systems where /etc/yum.repos.d is missing */
  if (access ("/etc/yum.repos.d", F_OK) != 0)
    {
      const char *tmp = g_get_tmp_dir ();
      g_autofree char *repos_dir = g_build_filename (tmp, "microdnf_rpm_cfg", "repos.d", NULL);
      g_mkdir_with_parents (repos_dir, 0755);
      const gchar *dirs[] = { repos_dir, NULL };
      dnf_context_set_repos_dir (ctx, dirs);
    }

  const GOptionEntry global_opts[] = {
    { "config", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Configuration file location", "<config file>" },
    { "disablerepo", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Disable repository by ID", "ID" },
    { "enablerepo", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Enable repository by ID", "ID" },
    { "installroot", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Set install root", "PATH" },
    { "refresh", '\0', 0, G_OPTION_ARG_NONE, &opt_refresh, "Set metadata as expired", NULL },
    { "releasever", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Override $releasever", "RELEASEVER" },
    { "reposdir", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Override repository configuration directory", "PATH" },
    { "setopt", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option,
      "Override a configuration option (excludepkgs=<glob>,..., best=<bool>, module_platform_id=<name:stream>, "
      "cachedir=<path>, reposdir=<path>,..., varsdir=<path>,..., repo_id.option_name=<value>)", "<opt>=<val>" },
    { NULL }
  };

  /* Is help option in arguments? */
  for (int in = 1; in < argc; in++)
    {
      if (g_strcmp0 (argv[in], "--") == 0)
        break;
      if (g_strcmp0 (argv[in], "-h") == 0 ||
          g_strcmp0 (argv[in], "--help") == 0 ||
          g_strcmp0 (argv[in], "--help-all") == 0 ||
          g_strcmp0 (argv[in], "--help-global") == 0)
        {
          g_autoptr(GOptionContext) help_ctx = g_option_context_new ("[OPTIONS...] <PACKAGE...>");
          GOptionGroup *help_global_group = g_option_group_new ("global", "Global Options:", "Show global options", ctx, NULL);
          g_option_group_add_entries (help_global_group, global_opts);
          g_option_context_add_group (help_ctx, help_global_group);
          const GOptionEntry entries[] = {
            { "output", 'o', 0, G_OPTION_ARG_NONE, NULL, "Output JSON lockfile path (defaults to stdout)", "FILE" },
            { "archlist", '\0', 0, G_OPTION_ARG_NONE, NULL, "Limit query to packages of given architectures", "ARCH,..." },
            { "forcearch", '\0', 0, G_OPTION_ARG_NONE, NULL, "Solve for this architecture instead of the host's (default: first non-noarch --archlist entry)", "ARCH" },
            { "exclude", 'x', 0, G_OPTION_ARG_NONE, NULL, "Exclude package and its dependencies unless directly requested", "PKG,..." },
            { "excludepkgs", '\0', 0, G_OPTION_ARG_NONE, NULL, "Hide packages matching NEVRA globs from the solver, like dnf --exclude/excludepkgs=", "GLOB,..." },
            { "module-enable", '\0', 0, G_OPTION_ARG_NONE, NULL, "Enable module streams before resolving, like dnf module enable", "NAME:STREAM,..." },
            { "best", '\0', 0, G_OPTION_ARG_NONE, NULL, "Require the best candidate for requested packages (default)", NULL },
            { "nobest", '\0', 0, G_OPTION_ARG_NONE, NULL, "Fall back to older candidates when the best one is not installable", NULL },
            { "lockfile", 'l', 0, G_OPTION_ARG_NONE, NULL, "Ordered list of lockfiles from previous layers", "FILE" },
            { "lock-file", 0, 0, G_OPTION_ARG_NONE, NULL, "Ordered list of lockfiles from previous layers", "FILE" },
            { NULL }
          };
          g_option_context_add_main_entries (help_ctx, entries, NULL);
          g_option_context_parse (help_ctx, &argc, &argv, NULL);
          return 0;
        }
    }

  /* Parse global options first so custom repo/cache settings take effect before context setup */
  g_autoptr(GOptionContext) global_opt_ctx = g_option_context_new (NULL);
  GOptionGroup *global_group = g_option_group_new ("global", "Global Options:", "Show global options", ctx, NULL);
  g_option_group_add_entries (global_group, global_opts);
  g_option_context_set_main_group (global_opt_ctx, global_group);
  g_option_context_set_ignore_unknown_options (global_opt_ctx, TRUE);
  g_option_context_set_help_enabled (global_opt_ctx, FALSE);

  g_autoptr(GError) error = NULL;
  if (!g_option_context_parse (global_opt_ctx, &argc, &argv, &error))
    {
      g_printerr ("Error parsing options: %s\n", error->message);
      return 1;
    }

  if (opt_refresh)
    dnf_context_set_cache_age (ctx, 0);

  /* Solve for the target architecture, not the host's: otherwise libsolv
   * rejects e.g. x86_64 packages on an arm64 host, and $basearch in repo URLs
   * would keep the host value. libdnf reads DNF_VAR_<name> as a repo variable
   * that overrides the built-in one. */
  g_autofree gchar *solve_arch = find_solve_arch (argc, argv);
  if (solve_arch)
    {
      dnf_context_set_arch (ctx, solve_arch);
      setenv ("DNF_VAR_basearch", basearch_for_arch (solve_arch), 1);
    }

  if (!dnf_context_setup (ctx, NULL, &error))
    {
      g_printerr ("Error setting up context: %s\n", error->message);
      return 1;
    }

  for (GSList *item = enable_disable_repos; item; item = item->next)
    {
      gchar *item_data = item->data;
      if (item_data[0] == 'd')
        dnf_context_repo_disable (ctx, item_data + 1, NULL);
      else
        dnf_context_repo_enable (ctx, item_data + 1, NULL);
    }

  g_autoptr(GOptionContext) subcmd_opt_ctx = g_option_context_new ("[OPTIONS...] <PACKAGE...>");
  GOptionGroup *subcmd_global_group = g_option_group_new ("global", "Global Options:", "Show global options", ctx, NULL);
  g_option_group_add_entries (subcmd_global_group, global_opts);
  g_option_context_add_group (subcmd_opt_ctx, subcmd_global_group);

  if (!run_lock (argc, argv, subcmd_opt_ctx, ctx, &error))
    {
      if (error)
        g_printerr ("lock error: %s\n", error->message);
      return 1;
    }

  return 0;
}
