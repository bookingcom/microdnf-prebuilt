/* dnf/plugins/mark/mark.c
 *
 * Implements marking requested RPM packages and minimal transitive dependencies into rpmdb.
 */

#include "mark.h"
#include "dnf-utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <glib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmmacro.h>
#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/solvable.h>

static gint
dnf_package_repo_prio_cost_cmp (gconstpointer a, gconstpointer b, gpointer user_data)
{
  DnfRepoLoader *repo_loader = user_data;
  DnfPackage *pkg1 = (DnfPackage *)a;
  DnfPackage *pkg2 = (DnfPackage *)b;

  const gchar *pkg1_reponame = dnf_package_get_reponame (pkg1);
  const gchar *pkg2_reponame = dnf_package_get_reponame (pkg2);

  DnfRepo *pkg1_repo = dnf_repo_loader_get_repo_by_id (repo_loader, pkg1_reponame, NULL);
  DnfRepo *pkg2_repo = dnf_repo_loader_get_repo_by_id (repo_loader, pkg2_reponame, NULL);

  gint result = (pkg2_repo ? hy_repo_get_priority (dnf_repo_get_repo (pkg2_repo)) : 0) -
                (pkg1_repo ? hy_repo_get_priority (dnf_repo_get_repo (pkg1_repo)) : 0);
  if (result == 0)
    result = (pkg1_repo ? hy_repo_get_cost (dnf_repo_get_repo (pkg1_repo)) : G_MAXINT) -
             (pkg2_repo ? hy_repo_get_cost (dnf_repo_get_repo (pkg2_repo)) : G_MAXINT);

  return result;
}

static GPtrArray *
select_one_pkg_for_nevra (DnfRepoLoader *repo_loader, GPtrArray *pkgs)
{
  g_autoptr(GHashTable) nevra2pkgs = g_hash_table_new (g_str_hash, g_str_equal);
  for (guint i = 0; i < pkgs->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (pkgs, i);
      const gchar *pkg_nevra = dnf_package_get_nevra (pkg);
      g_hash_table_insert (nevra2pkgs, (gpointer)pkg_nevra,
                           g_slist_prepend (g_hash_table_lookup (nevra2pkgs, pkg_nevra), pkg));
    }

  GPtrArray *pkgs_to_use = g_ptr_array_sized_new (g_hash_table_size (nevra2pkgs));
  GHashTableIter iter;
  GSList *pkgs_list;
  g_hash_table_iter_init (&iter, nevra2pkgs);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer)&pkgs_list))
    {
      pkgs_list = g_slist_sort_with_data (pkgs_list, dnf_package_repo_prio_cost_cmp, repo_loader);
      g_ptr_array_add (pkgs_to_use, g_object_ref (pkgs_list->data));
      g_slist_free (pkgs_list);
    }

  return pkgs_to_use;
}

static DnfPackage *
find_package_match (DnfContext *ctx, const char *spec, const char *opt_archlist, GError **error)
{
  DnfSack *sack = dnf_context_get_sack (ctx);
  DnfRepoLoader *repo_loader = dnf_context_get_repo_loader (ctx);

  g_autofree char *spec_name = NULL;
  g_autofree char *spec_ver = NULL;
  const char *eq = strchr (spec, '=');
  if (eq)
    {
      spec_name = g_strndup (spec, eq - spec);
      spec_ver = g_strdup (eq + 1);
    }

  if (spec_name && spec_ver)
    {
      hy_autoquery HyQuery q = hy_query_create (sack);
      hy_query_filter (q, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);
      hy_query_filter (q, HY_PKG_NAME, HY_EQ, spec_name);

      if (strchr (spec_ver, '-'))
        hy_query_filter (q, HY_PKG_EVR, HY_EQ, spec_ver);
      else
        hy_query_filter (q, HY_PKG_VERSION, HY_EQ, spec_ver);

      if (opt_archlist)
        {
          g_auto(GStrv) archs = g_strsplit_set (opt_archlist, ", ", -1);
          hy_query_filter_in (q, HY_PKG_ARCH, HY_EQ, (const char **)archs);
        }

      hy_query_filter_num (q, HY_PKG_LATEST_PER_ARCH_BY_PRIORITY, HY_EQ, 1);
      g_autoptr(GPtrArray) res = hy_query_run (q);

      if (res && res->len > 0)
        {
          g_autoptr(GPtrArray) selected = select_one_pkg_for_nevra (repo_loader, res);
          if (selected->len > 0)
            return g_object_ref (g_ptr_array_index (selected, 0));
        }
    }

  /* Try standard subject parsing */
  g_auto(HySubject) subject = hy_subject_create (spec);
  HyNevra out_nevra = NULL;
  hy_autoquery HyQuery subj_q = hy_subject_get_best_solution (subject, sack, NULL, &out_nevra,
                                                              FALSE, TRUE, TRUE, TRUE, FALSE);
  if (out_nevra)
    hy_nevra_free (out_nevra);

  hy_query_filter (subj_q, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);
  if (opt_archlist)
    {
      g_auto(GStrv) archs = g_strsplit_set (opt_archlist, ", ", -1);
      hy_query_filter_in (subj_q, HY_PKG_ARCH, HY_EQ, (const char **)archs);
    }
  hy_query_filter_num (subj_q, HY_PKG_LATEST_PER_ARCH_BY_PRIORITY, HY_EQ, 1);
  g_autoptr(GPtrArray) res = hy_query_run (subj_q);

  if (res && res->len > 0)
    {
      g_autoptr(GPtrArray) selected = select_one_pkg_for_nevra (repo_loader, res);
      if (selected->len > 0)
        return g_object_ref (g_ptr_array_index (selected, 0));
    }

  g_set_error (error, DNF_ERROR, DNF_ERROR_NO_SOLUTION,
               "No package matching '%s' found in repositories", spec);
  return NULL;
}

gboolean
run_mark (int argc, char **argv, GOptionContext *opt_ctx, DnfContext *ctx, GError **error)
{
  g_autofree gchar *opt_installroot = NULL;
  g_autofree gchar *opt_dbpath = NULL;
  g_autofree gchar *opt_archlist = NULL;
  g_auto(GStrv) opt_packages = NULL;
  g_auto(GStrv) opt_remaining = NULL;
  gboolean opt_nodeps = FALSE;

  const GOptionEntry entries[] = {
    { "installroot", '\0', 0, G_OPTION_ARG_FILENAME, &opt_installroot, "Root directory for rpmdb installation", "PATH" },
    { "dbpath", '\0', 0, G_OPTION_ARG_FILENAME, &opt_dbpath, "Path to rpm database (default: /var/lib/rpm)", "PATH" },
    { "archlist", '\0', 0, G_OPTION_ARG_STRING, &opt_archlist, "Limit query to packages of given architectures", "ARCH,..." },
    { "arch", '\0', 0, G_OPTION_ARG_STRING, &opt_archlist, "Limit query to packages of given architectures", "ARCH,..." },
    { "nodeps", '\0', 0, G_OPTION_ARG_NONE, &opt_nodeps, "Do not resolve dependencies", NULL },
    { "package", 'p', 0, G_OPTION_ARG_STRING_ARRAY, &opt_packages, "Package specification to mark as installed", "PKG[=VER]" },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_remaining, "Target packages", "PACKAGE..." },
    { NULL }
  };

  g_option_context_add_main_entries (opt_ctx, entries, NULL);
  if (!g_option_context_parse (opt_ctx, &argc, &argv, error))
    return FALSE;

  /* If installroot specified in subcommand, override ctx install root */
  if (opt_installroot)
    {
      g_autofree char *abs_root = g_canonicalize_filename (opt_installroot, NULL);
      g_free (opt_installroot);
      opt_installroot = g_steal_pointer (&abs_root);
      dnf_context_set_install_root (ctx, opt_installroot);
      g_mkdir_with_parents (opt_installroot, 0755);
    }
  else
    {
      const char *ctx_root = dnf_context_get_install_root (ctx);
      if (ctx_root && g_strcmp0 (ctx_root, "/") != 0)
        opt_installroot = g_canonicalize_filename (ctx_root, NULL);
    }

  if (!opt_installroot || g_strcmp0 (opt_installroot, "/") == 0)
    {
      g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                           "--installroot must be specified and cannot be '/'");
      return FALSE;
    }

  /* Collect requested package specs */
  g_autoptr(GPtrArray) specs = g_ptr_array_new_with_free_func (g_free);
  if (opt_packages)
    {
      for (char **p = opt_packages; *p; ++p)
        {
          g_auto(GStrv) tokens = g_strsplit_set (*p, ", ", -1);
          for (char **tok = tokens; *tok; ++tok)
            {
              g_strstrip (*tok);
              if (**tok)
                g_ptr_array_add (specs, g_strdup (*tok));
            }
        }
    }
  if (opt_remaining)
    {
      for (char **p = opt_remaining; *p; ++p)
        {
          g_auto(GStrv) tokens = g_strsplit_set (*p, ", ", -1);
          for (char **tok = tokens; *tok; ++tok)
            {
              g_strstrip (*tok);
              if (**tok)
                g_ptr_array_add (specs, g_strdup (*tok));
            }
        }
    }

  if (specs->len == 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Packages are not specified");
      return FALSE;
    }

  /* Set up sack with repositories (skip rpmdb to remain hermetic) */
  DnfState *state = dnf_context_get_state (ctx);
  if (!dnf_context_setup_sack_with_flags (ctx, state, DNF_CONTEXT_SETUP_SACK_FLAG_SKIP_RPMDB, error))
    return FALSE;

  DnfSack *sack = dnf_context_get_sack (ctx);
  DnfRepoLoader *repo_loader = dnf_context_get_repo_loader (ctx);

  /* Match target packages */
  g_autoptr(GPtrArray) target_pkgs = g_ptr_array_new_full (specs->len, (GDestroyNotify)g_object_unref);
  for (guint i = 0; i < specs->len; ++i)
    {
      const char *spec = g_ptr_array_index (specs, i);
      DnfPackage *pkg = find_package_match (ctx, spec, opt_archlist, error);
      if (!pkg)
        return FALSE;
      g_ptr_array_add (target_pkgs, pkg);
    }

  /* Resolve minimal transitive dependencies */
  g_autoptr(GPtrArray) all_pkgs = g_ptr_array_new_full (16, (GDestroyNotify)g_object_unref);
  for (guint i = 0; i < target_pkgs->len; ++i)
    {
      g_ptr_array_add (all_pkgs, g_object_ref (g_ptr_array_index (target_pkgs, i)));
    }

  if (!opt_nodeps)
    {
      HyGoal goal = hy_goal_create (sack);
      for (guint i = 0; i < target_pkgs->len; ++i)
        {
          hy_goal_install (goal, g_ptr_array_index (target_pkgs, i));
        }

      /* Run goal with DNF_IGNORE_WEAK_DEPS for minimal dependencies */
      if (hy_goal_run_flags (goal, DNF_IGNORE_WEAK_DEPS) == 0)
        {
          g_autoptr(GPtrArray) installs = hy_goal_list_installs (goal, NULL);
          if (installs)
            {
              for (guint i = 0; i < installs->len; ++i)
                {
                  DnfPackage *pkg = g_ptr_array_index (installs, i);
                  g_ptr_array_add (all_pkgs, g_object_ref (pkg));
                }
            }
        }
      else
        {
          /* Fallback: traverse requires within repository sack
           * This allows resolving within minimal repositories in multistage builds
           * without failing on external base OS requirements (e.g. libc.so.6) */
          g_autoptr(GHashTable) visited = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
          GQueue *queue = g_queue_new ();

          for (guint i = 0; i < target_pkgs->len; ++i)
            {
              DnfPackage *pkg = g_ptr_array_index (target_pkgs, i);
              const char *name = dnf_package_get_name (pkg);
              if (name && !g_hash_table_contains (visited, name))
                {
                  g_hash_table_add (visited, g_strdup (name));
                  g_queue_push_tail (queue, g_object_ref (pkg));
                }
            }

          hy_autoquery HyQuery dep_q = hy_query_create (sack);
          while (!g_queue_is_empty (queue))
            {
              g_autoptr(DnfPackage) cur = g_queue_pop_head (queue);
              DnfReldepList *reqs = dnf_package_get_requires (cur);
              if (!reqs)
                continue;

              int n_reqs = dnf_reldep_list_count (reqs);
              for (int r = 0; r < n_reqs; ++r)
                {
                  DnfReldep *reldep = dnf_reldep_list_index (reqs, r);
                  hy_query_filter_reldep (dep_q, HY_PKG_PROVIDES, reldep);
                  hy_query_filter (dep_q, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);
                  if (opt_archlist)
                    {
                      g_auto(GStrv) archs = g_strsplit_set (opt_archlist, ", ", -1);
                      hy_query_filter_in (dep_q, HY_PKG_ARCH, HY_EQ, (const char **)archs);
                    }
                  hy_query_filter_num (dep_q, HY_PKG_LATEST_PER_ARCH_BY_PRIORITY, HY_EQ, 1);
                  g_autoptr(GPtrArray) provs = hy_query_run (dep_q);
                  hy_query_clear (dep_q);
                  dnf_reldep_free (reldep);

                  if (provs && provs->len > 0)
                    {
                      DnfPackage *prov_pkg = g_ptr_array_index (provs, 0);
                      const char *pname = dnf_package_get_name (prov_pkg);
                      if (pname && !g_hash_table_contains (visited, pname))
                        {
                          g_hash_table_add (visited, g_strdup (pname));
                          g_ptr_array_add (all_pkgs, g_object_ref (prov_pkg));
                          g_queue_push_tail (queue, g_object_ref (prov_pkg));
                        }
                    }
                }
              dnf_reldep_list_free (reqs);
            }
          g_queue_free (queue);
        }
      hy_goal_free (goal);
    }

  /* Deduplicate packages */
  g_autoptr(GPtrArray) pkgs_to_mark = select_one_pkg_for_nevra (repo_loader, all_pkgs);

  /* Configure RPM database environment */
  const char *dbpath = opt_dbpath ? opt_dbpath : "/var/lib/rpm";
  rpmPushMacro (NULL, "_dbpath", NULL, dbpath, RMIL_CMDLINE);
  rpmPushMacro (NULL, "_db_backend", NULL, "sqlite", RMIL_CMDLINE);
  rpmPushMacro (NULL, "_rpmlock_path", NULL, "%{_dbpath}/.rpm.lock", RMIL_CMDLINE);

  const char *rel_dbpath = (dbpath[0] == '/') ? (dbpath + 1) : dbpath;
  g_autofree char *full_db_dir = g_build_filename (opt_installroot, rel_dbpath, NULL);
  g_mkdir_with_parents (full_db_dir, 0755);

  rpmts ts = rpmtsCreate ();
  rpmtsSetVSFlags (ts, RPMVSF_MASK_NOSIGNATURES);
  if (rpmtsSetRootDir (ts, opt_installroot) != 0)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to set RPM root directory to '%s'", opt_installroot);
      rpmtsFree (ts);
      return FALSE;
    }

  rpmtxn txn = rpmtxnBegin (ts, RPMTXN_WRITE);
  if (!txn)
    {
      g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                           "Failed to begin RPM transaction for database write");
      rpmtsFree (ts);
      return FALSE;
    }

  const char *download_dir = dnf_context_get_cache_dir (ctx);

  for (guint i = 0; i < pkgs_to_mark->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (pkgs_to_mark, i);
      const char *nevra = dnf_package_get_nevra (pkg);

      if (!dnf_package_get_repo (pkg))
        {
          const char *reponame = dnf_package_get_reponame (pkg);
          if (reponame)
            {
              DnfRepo *repo = dnf_repo_loader_get_repo_by_id (repo_loader, reponame, NULL);
              if (repo)
                dnf_package_set_repo (pkg, repo);
            }
        }

      g_autoptr(GError) dl_err = NULL;
      g_autofree gchar *rpm_path = dnf_package_download (pkg, download_dir, state, &dl_err);
      if (!rpm_path)
        {
          g_set_error (error, DNF_ERROR, DNF_ERROR_FAILED,
                       "Failed to download package '%s': %s",
                       nevra ? nevra : "unknown",
                       dl_err ? dl_err->message : "unknown error");
          rpmtxnEnd (txn);
          rpmtsFree (ts);
          return FALSE;
        }

      FD_t fd = Fopen (rpm_path, "r.ufdio");
      if (!fd)
        {
          g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                       "Cannot open downloaded RPM '%s'", rpm_path);
          rpmtxnEnd (txn);
          rpmtsFree (ts);
          return FALSE;
        }

      Header h = NULL;
      rpmRC rc = rpmReadPackageFile (ts, fd, rpm_path, &h);
      Fclose (fd);

      if ((rc != RPMRC_OK && rc != RPMRC_NOKEY && rc != RPMRC_NOTTRUSTED) || !h)
        {
          g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                       "Cannot read RPM header from '%s'", rpm_path);
          rpmtxnEnd (txn);
          rpmtsFree (ts);
          return FALSE;
        }

      if (!headerIsEntry (h, RPMTAG_INSTALLTIME))
        {
          uint32_t now = (uint32_t)time (NULL);
          headerPutUint32 (h, RPMTAG_INSTALLTIME, &now, 1);
        }
      if (!headerIsEntry (h, RPMTAG_INSTALLTID))
        {
          uint32_t tid = (uint32_t)time (NULL);
          headerPutUint32 (h, RPMTAG_INSTALLTID, &tid, 1);
        }

      rc = rpmtsImportHeader (txn, h, 0);
      headerFree (h);

      if (rc != RPMRC_OK)
        {
          g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                       "Failed to import header for '%s' into rpmdb",
                       nevra ? nevra : "unknown");
          rpmtxnEnd (txn);
          rpmtsFree (ts);
          return FALSE;
        }

      g_print ("Marked into rpmdb: %s\n", nevra ? nevra : "unknown");
    }

  rpmtxnEnd (txn);
  rpmtsCloseDB (ts);
  rpmtsFree (ts);

  return TRUE;
}
