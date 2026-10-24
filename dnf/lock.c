/* dnf/lock.c
 *
 * Implements dependency resolution and lockfile generation in JSON.
 */

#include "lock.h"
#include "lock-repo.h"
#include "dnf-utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <json-c/json.h>
#include <librepo/librepo.h>
#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/solvable.h>

typedef struct {
  gchar *name;
  gchar *version;
  gchar *arch;
  gchar *nevra;
  gchar *url;
  gchar *integrity;
  gchar *checksum;
  gboolean requested;
  GPtrArray *dependencies;          /* gchar* */
  GPtrArray *dependencies_nevra;    /* gchar* */
  GPtrArray *transitive_deps;       /* gchar* */
} InstalledPkg;

static void
installed_pkg_free (InstalledPkg *pkg)
{
  if (!pkg)
    return;
  g_free (pkg->name);
  g_free (pkg->version);
  g_free (pkg->arch);
  g_free (pkg->nevra);
  g_free (pkg->url);
  g_free (pkg->integrity);
  g_free (pkg->checksum);
  if (pkg->dependencies)
    g_ptr_array_unref (pkg->dependencies);
  if (pkg->dependencies_nevra)
    g_ptr_array_unref (pkg->dependencies_nevra);
  if (pkg->transitive_deps)
    g_ptr_array_unref (pkg->transitive_deps);
  g_free (pkg);
}

/* Identifies a package of a layer below: name.arch, as several arches of a
 * name can be installed at once, or the NEVRA for an installonly package,
 * whose versions are installed side by side. */
static gchar *
installed_key (const char *name, const char *arch, const char *nevra, const gchar **installonly)
{
  if (nevra && installonly && g_strv_contains (installonly, name))
    return g_strdup (nevra);
  return g_strdup_printf ("%s.%s", name, arch ? arch : "");
}

static InstalledPkg *
lookup_installed (GHashTable *installed_by_key, DnfPackage *pkg, const gchar **installonly)
{
  g_autofree gchar *key = installed_key (dnf_package_get_name (pkg), dnf_package_get_arch (pkg),
                                         dnf_package_get_nevra (pkg), installonly);
  return g_hash_table_lookup (installed_by_key, key);
}

static gchar *
compute_package_integrity (DnfPackage *pkg)
{
  int chk_type = 0;
  const unsigned char *chk_bytes = dnf_package_get_chksum (pkg, &chk_type);
  if (!chk_bytes)
    return NULL;

  int chk_len = 32;
  const char *algo_name = "sha256";
  switch (chk_type)
    {
    case G_CHECKSUM_SHA256: chk_len = 32; algo_name = "sha256"; break;
    case G_CHECKSUM_SHA512: chk_len = 64; algo_name = "sha512"; break;
    case G_CHECKSUM_SHA384: chk_len = 48; algo_name = "sha384"; break;
    case G_CHECKSUM_SHA1:   chk_len = 20; algo_name = "sha1"; break;
    case G_CHECKSUM_MD5:    chk_len = 16; algo_name = "md5"; break;
    default: chk_len = 32; algo_name = "sha256"; break;
    }

  gchar *b64 = g_base64_encode (chk_bytes, chk_len);
  gchar *integrity = g_strdup_printf ("%s-%s", algo_name, b64);
  g_free (b64);
  return integrity;
}

static gchar *
compute_package_checksum (DnfPackage *pkg)
{
  int chk_type = 0;
  const unsigned char *chk_bytes = dnf_package_get_chksum (pkg, &chk_type);
  if (!chk_bytes)
    return NULL;

  int chk_len = 32;
  switch (chk_type)
    {
    case G_CHECKSUM_SHA256: chk_len = 32; break;
    case G_CHECKSUM_SHA512: chk_len = 64; break;
    case G_CHECKSUM_SHA384: chk_len = 48; break;
    case G_CHECKSUM_SHA1:   chk_len = 20; break;
    case G_CHECKSUM_MD5:    chk_len = 16; break;
    default: chk_len = 32; break;
    }

  GString *hex = g_string_sized_new (chk_len * 2);
  for (int b = 0; b < chk_len; ++b)
    g_string_append_printf (hex, "%02x", chk_bytes[b]);
  return g_string_free (hex, FALSE);
}

static gboolean
parse_and_merge_lockfile (const gchar *path,
                          const gchar **installonly,
                          GHashTable *installed_by_key,
                          GHashTable *installed_pkgs,
                          GPtrArray *installed_order,
                          gchar **out_arch,
                          GError **error)
{
  g_autofree gchar *content = NULL;
  gsize length = 0;
  if (!g_file_get_contents (path, &content, &length, error))
    return FALSE;

  struct json_tokener *tok = json_tokener_new ();
  struct json_object *root = json_tokener_parse_ex (tok, content, length);
  enum json_tokener_error jerr = json_tokener_get_error (tok);
  json_tokener_free (tok);

  if (jerr != json_tokener_success || !root || !json_object_is_type (root, json_type_object))
    {
      if (root)
        json_object_put (root);
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to parse lockfile JSON at '%s'", path);
      return FALSE;
    }

  struct json_object *arch_obj = NULL;
  if (json_object_object_get_ex (root, "arch", &arch_obj))
    {
      if (out_arch && *out_arch == NULL)
        *out_arch = g_strdup (json_object_get_string (arch_obj));
    }

  struct json_object *pkgs_arr = NULL;
  if (json_object_object_get_ex (root, "packages", &pkgs_arr) &&
      json_object_is_type (pkgs_arr, json_type_array))
    {
      int n_pkgs = json_object_array_length (pkgs_arr);
      for (int i = 0; i < n_pkgs; ++i)
        {
          struct json_object *p_obj = json_object_array_get_idx (pkgs_arr, i);
          if (!p_obj || !json_object_is_type (p_obj, json_type_object))
            continue;

          struct json_object *name_obj = NULL;
          if (!json_object_object_get_ex (p_obj, "name", &name_obj))
            continue;
          const char *name = json_object_get_string (name_obj);
          if (!name || !*name)
            continue;

          InstalledPkg *pkg = g_new0 (InstalledPkg, 1);
          pkg->name = g_strdup (name);

          struct json_object *v_obj = NULL;
          if (json_object_object_get_ex (p_obj, "version", &v_obj))
            pkg->version = g_strdup (json_object_get_string (v_obj));

          struct json_object *a_obj = NULL;
          if (json_object_object_get_ex (p_obj, "arch", &a_obj))
            pkg->arch = g_strdup (json_object_get_string (a_obj));

          struct json_object *nevra_obj = NULL;
          if (json_object_object_get_ex (p_obj, "nevra", &nevra_obj))
            pkg->nevra = g_strdup (json_object_get_string (nevra_obj));

          struct json_object *url_obj = NULL;
          if (json_object_object_get_ex (p_obj, "url", &url_obj))
            pkg->url = g_strdup (json_object_get_string (url_obj));

          struct json_object *integ_obj = NULL;
          if (json_object_object_get_ex (p_obj, "integrity", &integ_obj))
            pkg->integrity = g_strdup (json_object_get_string (integ_obj));

          struct json_object *chk_obj = NULL;
          if (json_object_object_get_ex (p_obj, "checksum", &chk_obj))
            pkg->checksum = g_strdup (json_object_get_string (chk_obj));

          struct json_object *req_obj = NULL;
          if (json_object_object_get_ex (p_obj, "requested", &req_obj))
            pkg->requested = json_object_get_boolean (req_obj);

          /* Dependencies */
          struct json_object *deps_arr = NULL;
          if (json_object_object_get_ex (p_obj, "dependencies", &deps_arr) &&
              json_object_is_type (deps_arr, json_type_array))
            {
              pkg->dependencies = g_ptr_array_new_with_free_func (g_free);
              int nd = json_object_array_length (deps_arr);
              for (int d = 0; d < nd; ++d)
                {
                  struct json_object *d_item = json_object_array_get_idx (deps_arr, d);
                  if (d_item)
                    g_ptr_array_add (pkg->dependencies, g_strdup (json_object_get_string (d_item)));
                }
            }

          /* Dependencies NEVRA */
          struct json_object *deps_nevra_arr = NULL;
          if (json_object_object_get_ex (p_obj, "dependencies_nevra", &deps_nevra_arr) &&
              json_object_is_type (deps_nevra_arr, json_type_array))
            {
              pkg->dependencies_nevra = g_ptr_array_new_with_free_func (g_free);
              int nd = json_object_array_length (deps_nevra_arr);
              for (int d = 0; d < nd; ++d)
                {
                  struct json_object *d_item = json_object_array_get_idx (deps_nevra_arr, d);
                  if (d_item)
                    g_ptr_array_add (pkg->dependencies_nevra, g_strdup (json_object_get_string (d_item)));
                }
            }

          /* Transitive dependencies */
          struct json_object *trans_arr = NULL;
          if (json_object_object_get_ex (p_obj, "transitive_dependencies", &trans_arr) &&
              json_object_is_type (trans_arr, json_type_array))
            {
              pkg->transitive_deps = g_ptr_array_new_with_free_func (g_free);
              int nd = json_object_array_length (trans_arr);
              for (int d = 0; d < nd; ++d)
                {
                  struct json_object *d_item = json_object_array_get_idx (trans_arr, d);
                  if (d_item)
                    g_ptr_array_add (pkg->transitive_deps, g_strdup (json_object_get_string (d_item)));
                }
            }

          /* A later layer overrides an earlier one's package with the same key */
          gchar *key = installed_key (pkg->name, pkg->arch, pkg->nevra, installonly);
          InstalledPkg *old_pkg = g_hash_table_lookup (installed_by_key, key);
          if (old_pkg)
            g_ptr_array_remove (installed_order, old_pkg);
          /* Repoint the name before replace frees old_pkg, which it may name */
          g_hash_table_replace (installed_pkgs, g_strdup (name), pkg);
          g_hash_table_replace (installed_by_key, key, pkg);
          g_ptr_array_add (installed_order, pkg);
        }
    }

  json_object_put (root);
  return TRUE;
}

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

static HyQuery
get_packages_query (DnfContext *ctx, GStrv pkgs_spec, const gchar *opt_archlist)
{
  DnfSack *sack = dnf_context_get_sack (ctx);
  HyQuery query = hy_query_create (sack);

  if (pkgs_spec)
    {
      hy_query_filter_empty (query);
      for (char **pkey = pkgs_spec; *pkey; ++pkey)
        {
          g_auto(HySubject) subject = hy_subject_create (*pkey);
          HyNevra out_nevra = NULL;
          hy_autoquery HyQuery key_query =
            hy_subject_get_best_solution (subject, sack, NULL, &out_nevra,
                                          FALSE, TRUE, TRUE, TRUE, FALSE);
          if (out_nevra)
            hy_nevra_free (out_nevra);
          hy_query_filter (key_query, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);
          hy_query_filter_num (key_query, HY_PKG_LATEST_PER_ARCH_BY_PRIORITY, HY_EQ, 1);
          hy_query_union (query, key_query);
        }
    }

  if (opt_archlist)
    {
      g_auto(GStrv) archs_array = g_strsplit_set (opt_archlist, ", ", -1);
      hy_query_filter_in (query, HY_PKG_ARCH, HY_EQ, (const char **)archs_array);
    }

  return query;
}

static gchar *
resolve_package_url (DnfPackage *pkg, DnfContext *ctx)
{
  const char *loc = dnf_package_get_location (pkg);
  if (!loc)
    return g_strdup ("");

  if (strstr (loc, "://"))
    return g_strdup (loc);

  const char *pkg_base = dnf_package_get_baseurl (pkg);
  g_autofree gchar *repo_base_url = NULL;

  if (ctx)
    {
      const char *reponame = dnf_package_get_reponame (pkg);
      GPtrArray *repos = dnf_context_get_repos (ctx);
      if (repos)
        {
          for (guint i = 0; i < repos->len; ++i)
            {
              DnfRepo *repo = g_ptr_array_index (repos, i);
              const char *r_id = dnf_repo_get_id (repo);
              if (reponame && r_id && g_strcmp0 (reponame, r_id) != 0)
                continue;

              LrHandle *h = dnf_repo_get_lr_handle (repo);
              g_auto(GStrv) urls = NULL;
              if (h && lr_handle_getinfo (h, NULL, LRI_URLS, &urls) && urls && urls[0])
                {
                  repo_base_url = g_strdup (urls[0]);
                  break;
                }

              const char *repo_loc = dnf_repo_get_location (repo);
              if (repo_loc && repo_loc[0])
                {
                  if (strstr (repo_loc, "://"))
                    repo_base_url = g_strdup (repo_loc);
                  else
                    repo_base_url = g_strdup_printf ("file://%s%s", repo_loc[0] == '/' ? "" : "/", repo_loc);
                  break;
                }
            }
        }
    }

  g_autofree gchar *full_base = NULL;
  if (repo_base_url && pkg_base && !strstr (pkg_base, "://"))
    {
      gboolean r_slash = g_str_has_suffix (repo_base_url, "/");
      gboolean p_slash = (pkg_base[0] == '/');
      if (r_slash && p_slash)
        full_base = g_strdup_printf ("%s%s", repo_base_url, pkg_base + 1);
      else if (!r_slash && !p_slash)
        full_base = g_strdup_printf ("%s/%s", repo_base_url, pkg_base);
      else
        full_base = g_strdup_printf ("%s%s", repo_base_url, pkg_base);
    }
  else if (pkg_base && strstr (pkg_base, "://"))
    {
      full_base = g_strdup (pkg_base);
    }
  else if (repo_base_url)
    {
      full_base = g_strdup (repo_base_url);
    }
  else if (pkg_base)
    {
      full_base = g_strdup (pkg_base);
    }

  if (full_base)
    {
      gboolean base_has_slash = g_str_has_suffix (full_base, "/");
      gboolean loc_has_slash = (loc[0] == '/');
      if (base_has_slash && loc_has_slash)
        return g_strdup_printf ("%s%s", full_base, loc + 1);
      else if (!base_has_slash && !loc_has_slash)
        return g_strdup_printf ("%s/%s", full_base, loc);
      else
        return g_strdup_printf ("%s%s", full_base, loc);
    }

  return g_strdup (loc);
}

gboolean
run_lock (int argc, char **argv, GOptionContext *opt_ctx, DnfContext *ctx, GError **error)
{
  g_autofree gchar *opt_output = NULL;
  g_autofree gchar *opt_archlist = NULL;
  g_auto(GStrv) opt_exclude = NULL;
  g_auto(GStrv) opt_lockfiles = NULL;
  g_auto(GStrv) opt_packages = NULL;

  const GOptionEntry entries[] = {
    { "output", 'o', 0, G_OPTION_ARG_STRING, &opt_output, "Output JSON lockfile path (defaults to stdout)", "FILE" },
    { "archlist", '\0', 0, G_OPTION_ARG_STRING, &opt_archlist, "Limit query to packages of given architectures", "ARCH,..." },
    { "exclude", 'x', 0, G_OPTION_ARG_STRING_ARRAY, &opt_exclude, "Exclude package and its unneeded dependencies unless directly requested", "PKG,..." },
    { "lockfile", 'l', 0, G_OPTION_ARG_FILENAME_ARRAY, &opt_lockfiles, "Ordered list of lockfiles from previous layers", "FILE" },
    { "lock-file", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &opt_lockfiles, "Ordered list of lockfiles from previous layers", "FILE" },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_packages, "Target packages", "PACKAGE..." },
    { NULL }
  };

  g_option_context_add_main_entries (opt_ctx, entries, NULL);
  if (!g_option_context_parse (opt_ctx, &argc, &argv, error))
    return FALSE;

  /* Collect ordered list of lockfile paths from options and remaining arguments */
  g_autoptr(GPtrArray) ordered_lockfiles = g_ptr_array_new_with_free_func (g_free);
  if (opt_lockfiles)
    {
      for (char **lf = opt_lockfiles; *lf; ++lf)
        {
          g_auto(GStrv) tokens = g_strsplit_set (*lf, ",", -1);
          for (char **tok = tokens; *tok; ++tok)
            {
              g_strstrip (*tok);
              if (**tok)
                g_ptr_array_add (ordered_lockfiles, g_strdup (*tok));
            }
        }
    }

  g_autoptr(GPtrArray) actual_target_pkgs = g_ptr_array_new_with_free_func (g_free);
  if (opt_packages)
    {
      for (char **pkg_arg = opt_packages; *pkg_arg; ++pkg_arg)
        {
          if ((g_str_has_suffix (*pkg_arg, ".json") || g_str_has_suffix (*pkg_arg, ".lock.json")) &&
              g_file_test (*pkg_arg, G_FILE_TEST_IS_REGULAR))
            {
              g_ptr_array_add (ordered_lockfiles, g_strdup (*pkg_arg));
            }
          else
            {
              g_ptr_array_add (actual_target_pkgs, g_strdup (*pkg_arg));
            }
        }
    }

  if (actual_target_pkgs->len == 0)
    {
      g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                           "No packages specified for lockfile generation");
      return FALSE;
    }

  /* Parse and merge ordered lockfiles from previous layers */
  const gchar **installonly = dnf_context_get_installonly_pkgs (ctx);
  /* Owns the packages of the layers below; installed_pkgs maps a name to its latest one */
  g_autoptr(GHashTable) installed_by_key = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)installed_pkg_free);
  g_autoptr(GHashTable) installed_pkgs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_autoptr(GPtrArray) installed_order = g_ptr_array_new ();
  g_autofree gchar *inherited_arch = NULL;

  for (guint i = 0; i < ordered_lockfiles->len; ++i)
    {
      const char *lf_path = g_ptr_array_index (ordered_lockfiles, i);
      if (!parse_and_merge_lockfile (lf_path, installonly, installed_by_key, installed_pkgs, installed_order, &inherited_arch, error))
        return FALSE;
    }

  if (!opt_archlist && inherited_arch)
    opt_archlist = g_strdup (inherited_arch);

  /* Set of directly requested packages */
  g_autoptr(GHashTable) directly_requested = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  for (guint i = 0; i < actual_target_pkgs->len; ++i)
    {
      const char *pname = g_ptr_array_index (actual_target_pkgs, i);
      g_hash_table_add (directly_requested, g_strdup (pname));
    }

  DnfState *state = dnf_context_get_state (ctx);
  if (!dnf_context_setup_sack_with_flags (ctx, state, DNF_CONTEXT_SETUP_SACK_FLAG_SKIP_RPMDB, error))
    return FALSE;

  DnfSack *sack = dnf_context_get_sack (ctx);
  Pool *pool = dnf_sack_get_pool (sack);

  /* Create and populate system repo representing packages installed in previous layers */
  if (installed_order->len > 0)
    {
      Repo *sys_repo = lock_repo_create (pool, HY_SYSTEM_REPO_NAME);

      for (guint i = 0; i < installed_order->len; ++i)
        {
          InstalledPkg *inst = g_ptr_array_index (installed_order, i);

          /* Check if this package exists in remote repositories */
          hy_autoquery HyQuery pq = hy_query_create (sack);
          hy_query_filter (pq, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);
          hy_query_filter (pq, HY_PKG_NAME, HY_EQ, inst->name);
          if (inst->nevra)
            hy_query_filter (pq, HY_PKG_NEVRA, HY_EQ, inst->nevra);
          else if (inst->version)
            hy_query_filter (pq, HY_PKG_EVR, HY_EQ, inst->version);
          if (opt_archlist)
            {
              g_auto(GStrv) archs_array = g_strsplit_set (opt_archlist, ", ", -1);
              hy_query_filter_in (pq, HY_PKG_ARCH, HY_EQ, (const char **)archs_array);
            }
          g_autoptr(GPtrArray) cand_pkgs = hy_query_run (pq);

          if (cand_pkgs && cand_pkgs->len > 0)
            {
              DnfPackage *cand = g_ptr_array_index (cand_pkgs, 0);
              g_autofree gchar *cand_integrity = compute_package_integrity (cand);
              if (inst->integrity && cand_integrity && g_strcmp0 (inst->integrity, cand_integrity) != 0)
                {
                  /* RPM integrity changed! Do not mark as installed so solver fetches updated package */
                  continue;
                }

              /* Duplicate solvable to sys_repo */
              /* repo_add_solvable may reallocate pool->solvables, so look up both
               * solvables only after it. */
              Id new_id = repo_add_solvable (sys_repo);
              Solvable *s_cand = pool_id2solvable (pool, dnf_package_get_id (cand));
              Solvable *s_new = pool_id2solvable (pool, new_id);
              s_new->name = s_cand->name;
              s_new->arch = s_cand->arch;
              s_new->evr = s_cand->evr;
              s_new->vendor = s_cand->vendor;

              if (s_cand->provides)
                {
                  Id *dp = s_cand->repo->idarraydata + s_cand->provides;
                  while (*dp)
                    {
                      repo_add_deparray (sys_repo, new_id, SOLVABLE_PROVIDES, *dp, 0);
                      dp++;
                    }
                }
              if (s_cand->requires)
                {
                  Id *dp = s_cand->repo->idarraydata + s_cand->requires;
                  while (*dp)
                    {
                      repo_add_deparray (sys_repo, new_id, SOLVABLE_REQUIRES, *dp, 0);
                      dp++;
                    }
                }
              if (s_cand->conflicts)
                {
                  Id *dp = s_cand->repo->idarraydata + s_cand->conflicts;
                  while (*dp)
                    {
                      repo_add_deparray (sys_repo, new_id, SOLVABLE_CONFLICTS, *dp, 0);
                      dp++;
                    }
                }
              if (s_cand->obsoletes)
                {
                  Id *dp = s_cand->repo->idarraydata + s_cand->obsoletes;
                  while (*dp)
                    {
                      repo_add_deparray (sys_repo, new_id, SOLVABLE_OBSOLETES, *dp, 0);
                      dp++;
                    }
                }
            }
          else
            {
              /* Synthetic solvable from lockfile */
              Id new_id = repo_add_solvable (sys_repo);
              Solvable *s_new = pool_id2solvable (pool, new_id);
              s_new->name = pool_str2id (pool, inst->name, 1);
              s_new->arch = pool_str2id (pool, inst->arch ? inst->arch : "noarch", 1);
              s_new->evr = pool_str2id (pool, inst->version ? inst->version : "0", 1);
              s_new->vendor = 0;

              Id self_dep = pool_rel2id (pool, s_new->name, s_new->evr, REL_EQ, 1);
              repo_add_deparray (sys_repo, new_id, SOLVABLE_PROVIDES, self_dep, 0);
              repo_add_deparray (sys_repo, new_id, SOLVABLE_PROVIDES, s_new->name, 0);

              if (inst->dependencies)
                {
                  for (guint d = 0; d < inst->dependencies->len; ++d)
                    {
                      const char *dn = g_ptr_array_index (inst->dependencies, d);
                      repo_add_deparray (sys_repo, new_id, SOLVABLE_REQUIRES, pool_str2id (pool, dn, 1), 0);
                    }
                }
            }
        }

      repo_internalize (sys_repo);
      pool_set_installed (pool, sys_repo);
      lock_sack_repos_changed (sack);
    }

  /* Query remote repositories for target packages */
  g_auto(GStrv) pkgs_spec_array = (char **)g_new0 (char *, actual_target_pkgs->len + 1);
  for (guint i = 0; i < actual_target_pkgs->len; ++i)
    pkgs_spec_array[i] = g_strdup (g_ptr_array_index (actual_target_pkgs, i));

  hy_autoquery HyQuery query = get_packages_query (ctx, pkgs_spec_array, opt_archlist);
  g_autoptr(GPtrArray) target_pkgs = hy_query_run (query);

  for (guint i = 0; i < target_pkgs->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (target_pkgs, i);
      const char *name = dnf_package_get_name (pkg);
      if (name)
        g_hash_table_add (directly_requested, g_strdup (name));
    }

  gboolean all_requested_already_installed = TRUE;
  GHashTableIter r_it;
  gpointer r_name;
  g_hash_table_iter_init (&r_it, directly_requested);
  while (g_hash_table_iter_next (&r_it, &r_name, NULL))
    {
      if (!g_hash_table_contains (installed_pkgs, r_name))
        {
          all_requested_already_installed = FALSE;
          break;
        }
    }

  if (target_pkgs->len == 0 && !all_requested_already_installed)
    {
      g_set_error_literal (error, DNF_ERROR, DNF_ERROR_NO_SOLUTION,
                           "No packages matched the specified criteria");
      return FALSE;
    }

  g_autoptr(GPtrArray) all_pkgs = g_ptr_array_new_with_free_func ((GDestroyNotify)g_object_unref);

  if (target_pkgs->len > 0)
    {
      HyGoal goal = hy_goal_create (sack);
      for (guint i = 0; i < target_pkgs->len; ++i)
        {
          hy_goal_install (goal, g_ptr_array_index (target_pkgs, i));
        }

      if (hy_goal_run_flags (goal, DNF_NONE) != 0)
        {
          g_set_error_literal (error, DNF_ERROR, DNF_ERROR_NO_SOLUTION,
                               "Error resolving package dependencies");
          hy_goal_free (goal);
          return FALSE;
        }

      /* Only @System packages can be obsoleted, and a lockfile only adds packages */
      g_autoptr(GPtrArray) obsoleted = hy_goal_list_obsoleted (goal, NULL);
      for (guint i = 0; obsoleted && i < obsoleted->len; ++i)
        g_printerr ("warning: the solve obsoletes %s from a layer below; a lockfile cannot remove it, "
                    "so the image keeps it\n",
                    dnf_package_get_nevra (g_ptr_array_index (obsoleted, i)));

      /* Include target packages in all_pkgs ONLY if new or integrity changed */
      for (guint i = 0; i < target_pkgs->len; ++i)
        {
          DnfPackage *pkg = g_ptr_array_index (target_pkgs, i);
          InstalledPkg *inst = lookup_installed (installed_by_key, pkg, installonly);
          if (!inst)
            {
              g_ptr_array_add (all_pkgs, g_object_ref (pkg));
            }
          else
            {
              g_autofree gchar *integrity = compute_package_integrity (pkg);
              if (g_strcmp0 (integrity, inst->integrity) != 0)
                g_ptr_array_add (all_pkgs, g_object_ref (pkg));
            }
        }

      g_autoptr(GPtrArray) installs = hy_goal_list_installs (goal, NULL);
      if (installs)
        {
          for (guint i = 0; i < installs->len; ++i)
            {
              DnfPackage *pkg = g_ptr_array_index (installs, i);
              InstalledPkg *inst = lookup_installed (installed_by_key, pkg, installonly);
              if (!inst)
                {
                  g_ptr_array_add (all_pkgs, g_object_ref (pkg));
                }
              else
                {
                  g_autofree gchar *integrity = compute_package_integrity (pkg);
                  if (g_strcmp0 (integrity, inst->integrity) != 0)
                    g_ptr_array_add (all_pkgs, g_object_ref (pkg));
                }
            }
        }

      g_autoptr(GPtrArray) upgrades = hy_goal_list_upgrades (goal, NULL);
      if (upgrades)
        {
          for (guint i = 0; i < upgrades->len; ++i)
            {
              DnfPackage *pkg = g_ptr_array_index (upgrades, i);
              g_ptr_array_add (all_pkgs, g_object_ref (pkg));
            }
        }

      hy_goal_free (goal);
    }

  DnfRepoLoader *repo_loader = dnf_context_get_repo_loader (ctx);
  g_autoptr(GPtrArray) resolved = select_one_pkg_for_nevra (repo_loader, all_pkgs);
  g_ptr_array_set_free_func (resolved, g_object_unref);

  /* Build fast lookup maps: ID -> DnfPackage, and NEVRA -> DnfPackage, and NAME -> DnfPackage */
  g_autoptr(GHashTable) id_to_pkg = g_hash_table_new (g_direct_hash, g_direct_equal);
  g_autoptr(GHashTable) nevra_to_pkg = g_hash_table_new (g_str_hash, g_str_equal);
  g_autoptr(GHashTable) name_to_pkg = g_hash_table_new (g_str_hash, g_str_equal);

  for (guint i = 0; i < resolved->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (resolved, i);
      g_hash_table_insert (id_to_pkg, GUINT_TO_POINTER (dnf_package_get_id (pkg)), pkg);
      g_hash_table_insert (nevra_to_pkg, (gpointer)dnf_package_get_nevra (pkg), pkg);
      const char *name = dnf_package_get_name (pkg);
      if (name)
        g_hash_table_insert (name_to_pkg, (gpointer)name, pkg);
    }

  /* Parse exclude package names */
  g_autoptr(GHashTable) exclude_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  if (opt_exclude)
    {
      for (char **ex = opt_exclude; *ex; ++ex)
        {
          g_auto(GStrv) tokens = g_strsplit_set (*ex, ", ", -1);
          for (char **tok = tokens; *tok; ++tok)
            {
              g_strstrip (*tok);
              if (**tok)
                g_hash_table_add (exclude_set, g_strdup (*tok));
            }
        }
    }

  /* Discover direct dependencies of each resolved package */
  hy_autoquery HyQuery dep_query = hy_query_create (sack);
  g_autoptr(GHashTable) pkg_direct_deps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);
  g_autoptr(GHashTable) pkg_direct_nevras = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);

  for (guint i = 0; i < resolved->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (resolved, i);
      const char *pkg_name = dnf_package_get_name (pkg);
      if (!pkg_name)
        continue;

      GHashTable *dep_names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
      GHashTable *dep_nevras = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

      DnfReldepList *reqs = dnf_package_get_requires (pkg);
      if (reqs)
        {
          int n_reqs = dnf_reldep_list_count (reqs);
          for (int r = 0; r < n_reqs; ++r)
            {
              DnfReldep *reldep = dnf_reldep_list_index (reqs, r);
              hy_query_filter_reldep (dep_query, HY_PKG_PROVIDES, reldep);
              g_autoptr(GPtrArray) providers = hy_query_run (dep_query);
              hy_query_clear (dep_query);
              dnf_reldep_free (reldep);

              for (guint p = 0; p < providers->len; ++p)
                {
                  DnfPackage *prov = g_ptr_array_index (providers, p);
                  const char *prov_name = dnf_package_get_name (prov);
                  if (g_hash_table_contains (id_to_pkg, GUINT_TO_POINTER (dnf_package_get_id (prov))) ||
                      (prov_name && g_hash_table_contains (installed_pkgs, prov_name)))
                    {
                      if (g_strcmp0 (prov_name, pkg_name) != 0)
                        {
                          g_hash_table_add (dep_names, g_strdup (prov_name));
                          const char *pnevra = dnf_package_get_nevra (prov);
                          if (!pnevra && prov_name)
                            {
                              InstalledPkg *inst = g_hash_table_lookup (installed_pkgs, prov_name);
                              if (inst)
                                pnevra = inst->nevra;
                            }
                          if (pnevra)
                            g_hash_table_add (dep_nevras, g_strdup (pnevra));
                        }
                    }
                }
            }
          dnf_reldep_list_free (reqs);
        }

      g_hash_table_insert (pkg_direct_deps, g_strdup (pkg_name), dep_names);
      g_hash_table_insert (pkg_direct_nevras, g_strdup (pkg_name), dep_nevras);
    }

  /* Excluded packages: those in exclude_set UNLESS directly requested */
  g_autoptr(GHashTable) excluded_pkgs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  GHashTableIter ex_iter;
  gpointer ex_key;
  g_hash_table_iter_init (&ex_iter, exclude_set);
  while (g_hash_table_iter_next (&ex_iter, &ex_key, NULL))
    {
      if (!g_hash_table_contains (directly_requested, ex_key))
        g_hash_table_add (excluded_pkgs, g_strdup ((const char *)ex_key));
    }

  /* Graph reachability: packages reachable from directly_requested without going through excluded_pkgs */
  g_autoptr(GHashTable) reachable = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  GQueue *queue = g_queue_new ();

  gboolean any_in_reachable = FALSE;
  GHashTableIter req_iter;
  gpointer req_key;
  g_hash_table_iter_init (&req_iter, directly_requested);
  while (g_hash_table_iter_next (&req_iter, &req_key, NULL))
    {
      if (g_hash_table_contains (name_to_pkg, req_key))
        {
          g_hash_table_add (reachable, g_strdup ((const char *)req_key));
          g_queue_push_tail (queue, g_strdup ((const char *)req_key));
          any_in_reachable = TRUE;
        }
    }

  /* If directly requested packages are all already installed, keep new packages reachable */
  if (!any_in_reachable)
    {
      for (guint i = 0; i < resolved->len; ++i)
        {
          DnfPackage *pkg = g_ptr_array_index (resolved, i);
          const char *name = dnf_package_get_name (pkg);
          if (name && !g_hash_table_contains (excluded_pkgs, name) &&
              !g_hash_table_contains (reachable, name))
            {
              g_hash_table_add (reachable, g_strdup (name));
              g_queue_push_tail (queue, g_strdup (name));
            }
        }
    }

  while (!g_queue_is_empty (queue))
    {
      gchar *curr = g_queue_pop_head (queue);
      GHashTable *curr_deps = g_hash_table_lookup (pkg_direct_deps, curr);
      if (curr_deps)
        {
          GHashTableIter dep_it;
          gpointer dep_k;
          g_hash_table_iter_init (&dep_it, curr_deps);
          while (g_hash_table_iter_next (&dep_it, &dep_k, NULL))
            {
              const char *dep_name = dep_k;
              if (g_hash_table_contains (excluded_pkgs, dep_name))
                continue; /* Excluded: trim package and its exclusive dependencies */

              if (!g_hash_table_contains (reachable, dep_name) &&
                  g_hash_table_contains (name_to_pkg, dep_name))
                {
                  g_hash_table_add (reachable, g_strdup (dep_name));
                  g_queue_push_tail (queue, g_strdup (dep_name));
                }
            }
        }
      g_free (curr);
    }
  g_queue_free (queue);

  /* Build JSON lockfile document */
  struct json_object *root = json_object_new_object ();
  json_object_object_add (root, "version", json_object_new_int (1));
  if (opt_archlist)
    json_object_object_add (root, "arch", json_object_new_string (opt_archlist));

  /* Top-level requested RPM list */
  struct json_object *req_arr = json_object_new_array ();
  GList *req_list = g_hash_table_get_keys (directly_requested);
  req_list = g_list_sort (req_list, (GCompareFunc)strcmp);
  for (GList *l = req_list; l; l = l->next)
    json_object_array_add (req_arr, json_object_new_string ((const char *)l->data));
  g_list_free (req_list);
  json_object_object_add (root, "requested", req_arr);

  struct json_object *packages_arr = json_object_new_array ();

  for (guint i = 0; i < resolved->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (resolved, i);
      const char *name = dnf_package_get_name (pkg);
      if (!name || !g_hash_table_contains (reachable, name))
        continue; /* Trimmed package */

      const char *version = dnf_package_get_evr (pkg);
      const char *arch = dnf_package_get_arch (pkg);
      const char *nevra = dnf_package_get_nevra (pkg);

      g_autofree gchar *url = resolve_package_url (pkg, ctx);
      g_autofree gchar *integrity = compute_package_integrity (pkg);
      g_autofree gchar *hex_chksum = compute_package_checksum (pkg);

      /* Compute direct and transitive dependencies among kept reachable and installed packages */
      GHashTable *raw_deps = g_hash_table_lookup (pkg_direct_deps, name);
      GHashTable *raw_nevras = g_hash_table_lookup (pkg_direct_nevras, name);

      struct json_object *pkg_obj = json_object_new_object ();
      json_object_object_add (pkg_obj, "name", json_object_new_string (name ? name : ""));
      json_object_object_add (pkg_obj, "version", json_object_new_string (version ? version : ""));
      json_object_object_add (pkg_obj, "arch", json_object_new_string (arch ? arch : ""));
      json_object_object_add (pkg_obj, "nevra", json_object_new_string (nevra ? nevra : ""));
      json_object_object_add (pkg_obj, "url", json_object_new_string (url ? url : ""));
      json_object_object_add (pkg_obj, "integrity", json_object_new_string (integrity ? integrity : ""));
      if (hex_chksum)
        json_object_object_add (pkg_obj, "checksum", json_object_new_string (hex_chksum));

      gboolean is_req = g_hash_table_contains (directly_requested, name);
      json_object_object_add (pkg_obj, "requested", json_object_new_boolean (is_req));

      /* Direct dependencies */
      struct json_object *deps_arr = json_object_new_array ();
      struct json_object *deps_nevra_arr = json_object_new_array ();

      if (raw_deps)
        {
          GList *dk = g_hash_table_get_keys (raw_deps);
          dk = g_list_sort (dk, (GCompareFunc)strcmp);
          for (GList *l = dk; l; l = l->next)
            {
              if (g_hash_table_contains (reachable, l->data) ||
                  g_hash_table_contains (installed_pkgs, l->data))
                json_object_array_add (deps_arr, json_object_new_string ((const char *)l->data));
            }
          g_list_free (dk);
        }
      json_object_object_add (pkg_obj, "dependencies", deps_arr);

      if (raw_nevras)
        {
          GList *nk = g_hash_table_get_keys (raw_nevras);
          nk = g_list_sort (nk, (GCompareFunc)strcmp);
          for (GList *l = nk; l; l = l->next)
            json_object_array_add (deps_nevra_arr, json_object_new_string ((const char *)l->data));
          g_list_free (nk);
        }
      json_object_object_add (pkg_obj, "dependencies_nevra", deps_nevra_arr);

      /* Compute flat transitive dependencies */
      g_autoptr(GHashTable) trans_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
      GQueue *tq = g_queue_new ();
      g_queue_push_tail (tq, g_strdup (name));

      while (!g_queue_is_empty (tq))
        {
          gchar *curr_pkg = g_queue_pop_head (tq);
          GHashTable *curr_pkg_deps = g_hash_table_lookup (pkg_direct_deps, curr_pkg);
          if (curr_pkg_deps)
            {
              GHashTableIter tit;
              gpointer tk;
              g_hash_table_iter_init (&tit, curr_pkg_deps);
              while (g_hash_table_iter_next (&tit, &tk, NULL))
                {
                  const char *dep_n = tk;
                  if (g_strcmp0 (dep_n, name) != 0 &&
                      (g_hash_table_contains (reachable, dep_n) || g_hash_table_contains (installed_pkgs, dep_n)) &&
                      !g_hash_table_contains (trans_set, dep_n))
                    {
                      g_hash_table_add (trans_set, g_strdup (dep_n));
                      g_queue_push_tail (tq, g_strdup (dep_n));
                    }
                }
            }
          g_free (curr_pkg);
        }
      g_queue_free (tq);

      struct json_object *trans_arr = json_object_new_array ();
      GList *tk_list = g_hash_table_get_keys (trans_set);
      tk_list = g_list_sort (tk_list, (GCompareFunc)strcmp);
      for (GList *l = tk_list; l; l = l->next)
        json_object_array_add (trans_arr, json_object_new_string ((const char *)l->data));
      g_list_free (tk_list);
      json_object_object_add (pkg_obj, "transitive_dependencies", trans_arr);

      json_object_array_add (packages_arr, pkg_obj);
    }

  json_object_object_add (root, "packages", packages_arr);

  const char *json_str = json_object_to_json_string_ext (root, JSON_C_TO_STRING_PRETTY);
  g_autofree gchar *json_with_nl = g_strdup_printf ("%s\n", json_str);

  if (opt_output && strcmp (opt_output, "-") != 0)
    {
      if (!g_file_set_contents (opt_output, json_with_nl, -1, error))
        {
          json_object_put (root);
          return FALSE;
        }
    }
  else
    {
      g_print ("%s", json_with_nl);
    }

  json_object_put (root);
  return TRUE;
}
