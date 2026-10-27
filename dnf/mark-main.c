/* dnf/mark-main.c
 *
 * Standalone CLI tool to mark packages into rpmdb.
 */

#include "mark.h"
#include <stdio.h>
#include <locale.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmmacro.h>
#include <libdnf/libdnf.h>

static GSList *enable_disable_repos = NULL;
static gboolean opt_refresh = FALSE;
static gboolean opt_best = FALSE;
static gboolean opt_nobest = FALSE;
static gchar *opt_installroot_global = NULL;

/* Maps a solve architecture to dnf's $basearch (i686 -> i386, armv7hl -> armhfp, ...). */
static const gchar *
basearch_for_arch (const gchar *arch)
{
  if (arch[0] == 'i' && arch[1] != '\0' && g_str_has_suffix (arch, "86") && strlen (arch) == 4)
    return "i386";
  if (g_str_has_prefix (arch, "armv") && g_str_has_suffix (arch, "hl"))
    return "armhfp";
  return arch;
}

/* The architecture to solve for: --forcearch, else the first non-noarch/src
 * --archlist / --arch entry. Read from a copy of argv, ahead of dnf_context_setup,
 * so that $basearch in repo URLs follows it too. */
static gchar *
find_solve_arch (int argc, char **argv)
{
  g_autofree gchar *opt_archlist = NULL;
  g_autofree gchar *opt_forcearch = NULL;
  const GOptionEntry entries[] = {
    { "archlist", '\0', 0, G_OPTION_ARG_STRING, &opt_archlist, NULL, NULL },
    { "arch", '\0', 0, G_OPTION_ARG_STRING, &opt_archlist, NULL, NULL },
    { "forcearch", '\0', 0, G_OPTION_ARG_STRING, &opt_forcearch, NULL, NULL },
    { NULL }
  };

  g_autofree gchar **argv_copy = g_new0 (gchar *, argc + 1);
  for (int i = 0; i < argc; i++)
    argv_copy[i] = argv[i];
  int argc_copy = argc;
  gchar **argv_ptr = argv_copy;

  g_autoptr(GOptionContext) arch_ctx = g_option_context_new (NULL);
  g_option_context_add_main_entries (arch_ctx, entries, NULL);
  g_option_context_set_ignore_unknown_options (arch_ctx, TRUE);
  g_option_context_set_help_enabled (arch_ctx, FALSE);
  if (!g_option_context_parse (arch_ctx, &argc_copy, &argv_ptr, NULL))
    return NULL; /* run_mark reports the malformed option */

  if (opt_forcearch && *opt_forcearch)
    return g_steal_pointer (&opt_forcearch);
  if (opt_archlist)
    {
      g_auto(GStrv) archs = g_strsplit_set (opt_archlist, ", ", -1);
      for (char **a = archs; *a; ++a)
        {
          if (**a && g_strcmp0 (*a, "noarch") != 0 && g_strcmp0 (*a, "src") != 0)
            return g_strdup (*a);
        }
    }
  return NULL;
}

static gboolean
process_global_option (const gchar  *option_name,
                       const gchar  *value,
                       gpointer      data,
                       GError      **error)
{
  DnfContext *ctx = DNF_CONTEXT (data);

  if (g_strcmp0 (option_name, "--config") == 0 || g_strcmp0 (option_name, "--repo-config") == 0)
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
      g_free (opt_installroot_global);
      opt_installroot_global = g_canonicalize_filename (value, NULL);
      g_mkdir_with_parents (opt_installroot_global, 0755);
      dnf_context_set_install_root (ctx, opt_installroot_global);
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
  else if (g_strcmp0 (option_name, "--excludepkgs") == 0)
    {
      return dnf_conf_add_setopt ("excludepkgs", DNF_CONF_COMMANDLINE, value, error);
    }
  else if (g_strcmp0 (option_name, "--setopt") == 0)
    {
      g_auto(GStrv) setopt = g_strsplit (value, "=", 2);
      if (!setopt[0] || !setopt[1])
        {
          g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                       "Missing value in: %s", value);
          return FALSE;
        }
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
          return dnf_conf_add_setopt (setopt[0], DNF_CONF_COMMANDLINE, setopt[1], error);
        }
    }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  setlocale (LC_ALL, "");

  /* Create an isolated temporary workspace so nothing is ever read from / */
  g_autoptr(GError) error = NULL;
  g_autofree char *work_dir = g_dir_make_tmp ("mark_work_XXXXXX", &error);
  if (!work_dir)
    {
      g_printerr ("Failed to create temporary directory: %s\n", error ? error->message : "unknown");
      return 1;
    }

  if (!getenv ("RPM_CONFIGDIR"))
    {
      g_autofree char *rpm_cfg_dir = g_build_filename (work_dir, "rpm_cfg", NULL);
      g_mkdir_with_parents (rpm_cfg_dir, 0755);
      g_autofree char *rcfile = g_build_filename (rpm_cfg_dir, "rpmrc", NULL);
      g_file_set_contents (rcfile, "", 0, NULL);
      g_autofree char *macfile = g_build_filename (rpm_cfg_dir, "macros", NULL);
      g_file_set_contents (macfile, "", 0, NULL);
      setenv ("RPM_CONFIGDIR", rpm_cfg_dir, 1);
    }

  rpmPushMacro (NULL, "_dbpath", NULL, "/var/lib/rpm", RMIL_DEFAULT);
  rpmPushMacro (NULL, "_db_backend", NULL, "sqlite", RMIL_DEFAULT);
  rpmPushMacro (NULL, "_rpmlock_path", NULL, "%{_dbpath}/.rpm.lock", RMIL_DEFAULT);

  /* Disable reading host /etc/dnf/dnf.conf and /usr/lib64/libdnf/plugins */
  dnf_context_set_config_file_path ("");
  dnf_context_set_plugins_all_disabled (TRUE);

  g_autoptr(DnfContext) ctx = dnf_context_new ();

  g_autofree gchar *metadatadir = g_build_filename (work_dir, "cache", "metadata", NULL);
  g_autofree gchar *solvdir = g_build_filename (work_dir, "cache", "solv", NULL);
  g_autofree gchar *lockdir = g_build_filename (work_dir, "cache", "lock", NULL);

  dnf_context_set_cache_dir (ctx, metadatadir);
  dnf_context_set_solv_dir (ctx, solvdir);
  dnf_context_set_lock_dir (ctx, lockdir);

  const gchar *empty_dirs[] = { NULL };
  dnf_context_set_repos_dir (ctx, empty_dirs);
  dnf_context_set_vars_dir (ctx, empty_dirs);

  dnf_context_set_check_disk_space (ctx, FALSE);
  dnf_context_set_check_transaction (ctx, FALSE);
  dnf_context_set_cache_age (ctx, G_MAXUINT - 1);

  const GOptionEntry global_opts[] = {
    { "best", '\0', 0, G_OPTION_ARG_NONE, &opt_best, "Try the best available package versions in transactions", NULL },
    { "nobest", '\0', 0, G_OPTION_ARG_NONE, &opt_nobest, "Do not limit the transaction to the best candidates", NULL },
    { "config", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Configuration file location", "<config file>" },
    { "repo-config", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Repository configuration file location", "<config file>" },
    { "disablerepo", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Disable repository by ID", "ID" },
    { "enablerepo", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Enable repository by ID", "ID" },
    { "excludepkgs", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Hide packages matching NEVRA globs from the solver", "GLOB,..." },
    { "installroot", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Set install root", "PATH" },
    { "refresh", '\0', 0, G_OPTION_ARG_NONE, &opt_refresh, "Set metadata as expired", NULL },
    { "releasever", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Override $releasever", "RELEASEVER" },
    { "reposdir", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Override repository configuration directory", "PATH" },
    { "setopt", '\0', 0, G_OPTION_ARG_CALLBACK, process_global_option, "Override configuration option", "<opt>=<val>" },
    { NULL }
  };

  /* Help check */
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
          g_autoptr(GOptionGroup) help_global_group = g_option_group_new ("global", "Global Options:", "Show global options", ctx, NULL);
          g_option_group_add_entries (help_global_group, global_opts);
          g_option_context_add_group (help_ctx, g_steal_pointer (&help_global_group));
          const GOptionEntry entries[] = {
            { "installroot", '\0', 0, G_OPTION_ARG_NONE, NULL, "Root directory for rpmdb installation", "PATH" },
            { "dbpath", '\0', 0, G_OPTION_ARG_NONE, NULL, "Path to rpm database (default: /var/lib/rpm)", "PATH" },
            { "archlist", '\0', 0, G_OPTION_ARG_NONE, NULL, "Limit query to packages of given architectures", "ARCH,..." },
            { "arch", '\0', 0, G_OPTION_ARG_NONE, NULL, "Limit query to packages of given architectures", "ARCH,..." },
            { "forcearch", '\0', 0, G_OPTION_ARG_NONE, NULL, "Solve for this architecture instead of the host's (default: first non-noarch --archlist entry)", "ARCH" },
            { "nodeps", '\0', 0, G_OPTION_ARG_NONE, NULL, "Do not resolve dependencies", NULL },
            { "package", 'p', 0, G_OPTION_ARG_NONE, NULL, "Package specification to mark as installed", "PKG[=VER]" },
            { NULL }
          };
          g_option_context_add_main_entries (help_ctx, entries, NULL);
          char *help_argv[] = { argv[0], argv[in], NULL };
          int help_argc = 2;
          char **help_argv_ptr = help_argv;
          g_option_context_parse (help_ctx, &help_argc, &help_argv_ptr, NULL);
          return 0;
        }
    }

  /* Parse global options first */
  g_autoptr(GOptionContext) global_opt_ctx = g_option_context_new (NULL);
  g_autoptr(GOptionGroup) global_group = g_option_group_new ("global", "Global Options:", "Show global options", ctx, NULL);
  g_option_group_add_entries (global_group, global_opts);
  g_option_context_set_main_group (global_opt_ctx, g_steal_pointer (&global_group));
  g_option_context_set_ignore_unknown_options (global_opt_ctx, TRUE);
  g_option_context_set_help_enabled (global_opt_ctx, FALSE);

  if (!g_option_context_parse (global_opt_ctx, &argc, &argv, &error))
    {
      g_printerr ("Error parsing options: %s\n", error->message);
      return 1;
    }

  if (opt_best && opt_nobest)
    {
      g_printerr ("Error: Argument --nobest is not allowed with argument --best\n");
      return 1;
    }

  if (!opt_installroot_global || g_strcmp0 (opt_installroot_global, "/") == 0)
    {
      g_printerr ("Error: --installroot must be specified and cannot be '/'\n");
      return 1;
    }

  if (opt_refresh)
    dnf_context_set_cache_age (ctx, 0);

  /* Solve for the target architecture, not the host's: otherwise libsolv
   * rejects e.g. x86_64 packages on an arm64 host, and $basearch in repo URLs
   * would keep the host value. */
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

  if (opt_best)
    dnf_context_set_best (TRUE);
  else if (opt_nobest)
    dnf_context_set_best (FALSE);

  for (GSList *item = enable_disable_repos; item; item = item->next)
    {
      gchar *item_data = item->data;
      if (item_data[0] == 'd')
        dnf_context_repo_disable (ctx, item_data + 1, NULL);
      else
        dnf_context_repo_enable (ctx, item_data + 1, NULL);
    }

  g_autoptr(GOptionContext) subcmd_opt_ctx = g_option_context_new ("[OPTIONS...] <PACKAGE...>");
  g_autoptr(GOptionGroup) subcmd_global_group = g_option_group_new ("global", "Global Options:", "Show global options", ctx, NULL);
  g_option_group_add_entries (subcmd_global_group, global_opts);
  g_option_context_add_group (subcmd_opt_ctx, g_steal_pointer (&subcmd_global_group));

  gboolean ok = run_mark (argc, argv, subcmd_opt_ctx, ctx, &error);
  g_slist_free_full (enable_disable_repos, g_free);
  g_free (opt_installroot_global);

  if (!ok)
    {
      if (error)
        g_printerr ("mark error: %s\n", error->message);
      return 1;
    }

  return 0;
}
