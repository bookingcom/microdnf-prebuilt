/* dnf/lock.c
 *
 * Implements dependency resolution and lockfile generation in JSON.
 */

#include "lock.h"
#include "lock-goal.h"
#include "dnf-utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <json-c/json.h>
#include <librepo/librepo.h>

static void
ptr_array_extend_and_steal (GPtrArray *array_to_extend, GPtrArray *array)
{
  guint origin_len = array_to_extend->len;
  g_ptr_array_set_size (array_to_extend, origin_len + array->len);
  for (guint i = 0; i < array->len; ++i)
    {
      g_ptr_array_index (array_to_extend, origin_len + i) = g_ptr_array_index (array, i);
    }
  array->len = 0;
  g_ptr_array_unref (array);
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

static void
filter_archlist (HyQuery query, const gchar *archlist)
{
  g_auto(GStrv) archs = g_strsplit_set (archlist, ", ", -1);
  hy_query_filter_in (query, HY_PKG_ARCH, HY_EQ, (const char **)archs);
}

/* Packages, installed or available, that @spec names (NEVRA forms, globs, provides). */
static HyQuery
spec_solution_query (DnfSack *sack, const gchar *spec)
{
  g_auto(HySubject) subject = hy_subject_create (spec);
  HyNevra out_nevra = NULL;
  HyQuery query = hy_subject_get_best_solution (subject, sack, NULL, &out_nevra,
                                                FALSE, TRUE, TRUE, TRUE, FALSE);
  if (out_nevra)
    hy_nevra_free (out_nevra);
  return query;
}

/* Query for one package spec, like `dnf install <spec>`: system packages are
 * left out; with @latest only the newest candidate per arch survives. The
 * @opt_archlist restriction, if any, is applied last. */
static HyQuery
get_spec_query (DnfSack *sack, const gchar *spec, gboolean latest, const gchar *opt_archlist)
{
  HyQuery key_query = spec_solution_query (sack, spec);
  hy_query_filter (key_query, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);
  if (latest)
    hy_query_filter_num (key_query, HY_PKG_LATEST_PER_ARCH_BY_PRIORITY, HY_EQ, 1);
  if (opt_archlist)
    filter_archlist (key_query, opt_archlist);
  return key_query;
}

/* TRUE when @spec names an installed package (`dnf install` reports it as already installed). */
static gboolean
spec_is_installed (DnfSack *sack, const gchar *spec)
{
  hy_autoquery HyQuery query = spec_solution_query (sack, spec);
  hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
  return !hy_query_is_empty (query);
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
          hy_autoquery HyQuery key_query = get_spec_query (sack, *pkey, TRUE, NULL);
          hy_query_union (query, key_query);
        }
    }

  if (opt_archlist)
    filter_archlist (query, opt_archlist);

  return query;
}

/* Joins @base and @path with exactly one "/" between them. */
static gchar *
join_url_path (const char *base, const char *path)
{
  gboolean base_slash = g_str_has_suffix (base, "/");
  gboolean path_slash = (path[0] == '/');
  if (base_slash && path_slash)
    return g_strdup_printf ("%s%s", base, path + 1);
  if (!base_slash && !path_slash)
    return g_strdup_printf ("%s/%s", base, path);
  return g_strdup_printf ("%s%s", base, path);
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
              char **urls = NULL;
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
      full_base = join_url_path (repo_base_url, pkg_base);
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

  gchar *res = NULL;
  if (full_base)
    res = join_url_path (full_base, loc);
  else
    res = g_strdup (loc);

  if (g_str_has_prefix (res, "file://"))
    {
      const char *path = res + 7;
      const char *ws = g_getenv ("BUILD_WORKSPACE_DIRECTORY");
      if (!ws || !*ws)
        ws = g_getenv ("PWD");

      if (ws && *ws && g_str_has_prefix (path, ws))
        {
          const char *rel = path + strlen (ws);
          while (*rel == '/')
            rel++;
          g_free (res);
          return g_strdup (rel);
        }

      const char *execroot_marker = strstr (path, "/execroot/");
      if (execroot_marker)
        {
          const char *after_root = strchr (execroot_marker + 10, '/');
          if (after_root)
            {
              while (*after_root == '/')
                after_root++;
              g_free (res);
              return g_strdup (after_root);
            }
        }

      const char *home = g_get_home_dir ();
      if (home && *home && g_str_has_prefix (path, home))
        {
          const char *e2e = strstr (path, "/e2e/");
          if (e2e)
            {
              g_free (res);
              return g_strdup (e2e + 1);
            }
        }
    }

  return res;
}

/* Splits repeated "a,b c" option values into a NULL-terminated, de-duplicated list. */
static GStrv
split_option_values (GStrv values)
{
  g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func (g_free);
  if (values)
    {
      for (char **val = values; *val; ++val)
        {
          g_auto(GStrv) tokens = g_strsplit_set (*val, ", ", -1);
          for (char **tok = tokens; *tok; ++tok)
            {
              g_strstrip (*tok);
              if (**tok && !g_ptr_array_find_with_equal_func (out, *tok, g_str_equal, NULL))
                g_ptr_array_add (out, g_strdup (*tok));
            }
        }
    }
  g_ptr_array_add (out, NULL);
  return (GStrv)g_ptr_array_free (g_steal_pointer (&out), FALSE);
}

/* Parses a boolean option value the way libdnf/microdnf do: 1/0, yes/no,
 * true/false, on/off, case-insensitive. Anything else is an error. */
gboolean
lock_parse_bool (const gchar *name, const gchar *value, gboolean default_value,
                 gboolean *out, GError **error)
{
  if (!value || !*value)
    {
      *out = default_value;
      return TRUE;
    }
  static const gchar *const truthy[] = { "1", "yes", "true", "on", NULL };
  static const gchar *const falsy[] = { "0", "no", "false", "off", NULL };
  for (const gchar *const *v = truthy; *v; ++v)
    if (g_ascii_strcasecmp (value, *v) == 0)
      {
        *out = TRUE;
        return TRUE;
      }
  for (const gchar *const *v = falsy; *v; ++v)
    if (g_ascii_strcasecmp (value, *v) == 0)
      {
        *out = FALSE;
        return TRUE;
      }
  g_set_error (error, DNF_ERROR, DNF_ERROR_FAILED,
               "Invalid boolean value '%s' for %s (expected 1/0, yes/no, true/false or on/off)",
               value, name);
  return FALSE;
}

/* dnf's "best" is opt-in in libdnf (default False) but every EL dnf.conf sets
 * best=True, and the lockfile has always pinned the newest candidate. Keep
 * that unless --nobest was given or "best" was set explicitly (config file,
 * --setopt=best=..., microdnf --best/--nobest). */
static gboolean
lock_use_best (gboolean opt_best, gboolean opt_nobest)
{
  if (opt_nobest)
    return FALSE;
  if (opt_best)
    return TRUE;
  enum DnfConfPriority prio = DNF_CONF_EMPTY;
  g_autofree gchar *val = dnf_conf_main_get_option ("best", &prio, NULL);
  if (val && prio > DNF_CONF_DEFAULT)
    return dnf_context_get_best ();
  return TRUE;
}

/* Whether an installed package already satisfies a request for @pkg the way
 * `dnf install` reports "already installed": same name and arch, and the same
 * version, or a newer one unless the request pinned @pkg's version explicitly
 * (`dnf install foo-1.0` on a newer foo is a downgrade). */
static gboolean
is_satisfied_by_installed (DnfSack *sack, DnfPackage *pkg, gboolean pinned)
{
  hy_autoquery HyQuery q = hy_query_create (sack);
  hy_query_filter (q, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
  hy_query_filter (q, HY_PKG_NAME, HY_EQ, dnf_package_get_name (pkg));
  hy_query_filter (q, HY_PKG_ARCH, HY_EQ, dnf_package_get_arch (pkg));
  g_autoptr(GPtrArray) installed = hy_query_run (q);
  for (guint i = 0; i < installed->len; ++i)
    {
      int cmp = dnf_package_evr_cmp (g_ptr_array_index (installed, i), pkg);
      if (cmp == 0 || (cmp > 0 && !pinned))
        return TRUE;
    }
  return FALSE;
}

/* Whether @root carries an rpmdb (sqlite, ndb or bdb; /var/lib/rpm or
 * /usr/lib/sysimage/rpm). A root without one, e.g. a scratch image, has an
 * empty installed set; it is not handed to librpm, which would create one. */
static gboolean
installroot_has_rpmdb (const gchar *root)
{
  const gchar *dirs[] = { "var/lib/rpm", "usr/lib/sysimage/rpm" };
  const gchar *files[] = { "rpmdb.sqlite", "Packages.db", "Packages" };
  for (guint d = 0; d < G_N_ELEMENTS (dirs); ++d)
    for (guint f = 0; f < G_N_ELEMENTS (files); ++f)
      {
        g_autofree gchar *path = g_build_filename (root, dirs[d], files[f], NULL);
        if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
          return TRUE;
      }
  return FALSE;
}

/* TRUE when @root has an os-release file, where libdnf finds PLATFORM_ID. */
static gboolean
installroot_has_os_release (const gchar *root)
{
  const gchar *paths[] = { "etc/os-release", "usr/lib/os-release" };
  for (guint i = 0; i < G_N_ELEMENTS (paths); ++i)
    {
      g_autofree gchar *path = g_build_filename (root, paths[i], NULL);
      if (g_file_test (path, G_FILE_TEST_EXISTS))
        return TRUE;
    }
  return FALSE;
}

/* Adds edges pkg -> providers of @reqs. @resolved_query is an applied query
 * holding only the resolved packages; each lookup runs on a clone of it, so
 * the provider search never leaves the resolved set (and never scans the sack). */
static void
add_reldep_edges (HyQuery resolved_query, DnfReldepList *reqs,
                  const char *pkg_name, GHashTable *dep_names, GHashTable *dep_nevras)
{
  if (!reqs)
    return;
  int n_reqs = dnf_reldep_list_count (reqs);
  for (int r = 0; r < n_reqs; ++r)
    {
      DnfReldep *reldep = dnf_reldep_list_index (reqs, r);
      hy_autoquery HyQuery q = hy_query_clone (resolved_query);
      hy_query_filter_reldep (q, HY_PKG_PROVIDES, reldep);
      g_autoptr(GPtrArray) providers = hy_query_run (q);
      dnf_reldep_free (reldep);

      for (guint p = 0; p < providers->len; ++p)
        {
          DnfPackage *prov = g_ptr_array_index (providers, p);
          const char *prov_name = dnf_package_get_name (prov);
          if (g_strcmp0 (prov_name, pkg_name) == 0)
            continue;
          g_hash_table_add (dep_names, g_strdup (prov_name));
          if (dep_nevras)
            g_hash_table_add (dep_nevras, g_strdup (dnf_package_get_nevra (prov)));
        }
    }
  dnf_reldep_list_free (reqs);
}

/* A set of owned strings. */
static GHashTable *
string_set_new (void)
{
  return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/* The sorted contents of the string set @set as a JSON array, limited to
 * entries also in @only_in (if given) and without @skip (if given). */
static struct json_object *
sorted_set_array (GHashTable *set, GHashTable *only_in, const char *skip)
{
  struct json_object *arr = json_object_new_array ();
  if (!set)
    return arr;
  GList *keys = g_list_sort (g_hash_table_get_keys (set), (GCompareFunc)strcmp);
  for (GList *l = keys; l; l = l->next)
    {
      if (only_in && !g_hash_table_contains (only_in, l->data))
        continue;
      if (skip && g_strcmp0 (l->data, skip) == 0)
        continue;
      json_object_array_add (arr, json_object_new_string ((const char *)l->data));
    }
  g_list_free (keys);
  return arr;
}

/* Marks @name reachable and queues it for expansion, unless already marked. */
static void
mark_reachable (GHashTable *reachable, GQueue *queue, const char *name)
{
  if (g_hash_table_contains (reachable, name))
    return;
  g_hash_table_add (reachable, g_strdup (name));
  g_queue_push_tail (queue, g_strdup (name));
}

/* Appends a new reference to every element of @src (which may be NULL) to @dst. */
static void
ptr_array_add_refs (GPtrArray *dst, GPtrArray *src)
{
  for (guint i = 0; src && i < src->len; ++i)
    g_ptr_array_add (dst, g_object_ref (g_ptr_array_index (src, i)));
}

static GHashTable *
lookup_or_new_set (GHashTable *map, const char *key)
{
  GHashTable *set = g_hash_table_lookup (map, key);
  if (!set)
    {
      set = string_set_new ();
      g_hash_table_insert (map, g_strdup (key), set);
    }
  return set;
}

static struct json_object *
sorted_nevra_array (GPtrArray *pkgs)
{
  g_autoptr(GPtrArray) nevras = g_ptr_array_new ();
  for (guint i = 0; pkgs && i < pkgs->len; ++i)
    g_ptr_array_add (nevras, (gpointer)dnf_package_get_nevra (g_ptr_array_index (pkgs, i)));
  g_ptr_array_sort_values (nevras, (GCompareFunc)strcmp);
  struct json_object *arr = json_object_new_array ();
  for (guint i = 0; i < nevras->len; ++i)
    {
      if (i > 0 && strcmp (g_ptr_array_index (nevras, i), g_ptr_array_index (nevras, i - 1)) == 0)
        continue;
      json_object_array_add (arr, json_object_new_string (g_ptr_array_index (nevras, i)));
    }
  return arr;
}

gboolean
run_lock (int argc, char **argv, GOptionContext *opt_ctx, DnfContext *ctx, GError **error)
{
  g_autofree gchar *opt_output = NULL;
  g_autofree gchar *opt_archlist = NULL;
  g_autofree gchar *opt_forcearch = NULL;
  g_auto(GStrv) opt_exclude = NULL;
  g_auto(GStrv) opt_packages = NULL;
  g_auto(GStrv) opt_excludepkgs = NULL;
  g_auto(GStrv) opt_module_enable = NULL;
  gboolean opt_best = FALSE;
  gboolean opt_nobest = FALSE;

  const GOptionEntry entries[] = {
    { "output", 'o', 0, G_OPTION_ARG_STRING, &opt_output, "Output JSON lockfile path (defaults to stdout)", "FILE" },
    { "archlist", '\0', 0, G_OPTION_ARG_STRING, &opt_archlist, "Limit query to packages of given architectures", "ARCH,..." },
    { "forcearch", '\0', 0, G_OPTION_ARG_STRING, &opt_forcearch, "Solve for this architecture instead of the host's (default: first non-noarch --archlist entry)", "ARCH" },
    { "exclude", 'x', 0, G_OPTION_ARG_STRING_ARRAY, &opt_exclude, "Exclude package and its unneeded dependencies unless directly requested", "PKG,..." },
    { "excludepkgs", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &opt_excludepkgs, "Hide packages matching NEVRA globs from the solver, like dnf --exclude/excludepkgs=", "GLOB,..." },
    { "module-enable", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &opt_module_enable, "Enable module streams before resolving, like dnf module enable", "NAME:STREAM,..." },
    { "best", '\0', 0, G_OPTION_ARG_NONE, &opt_best, "Require the best candidate for requested packages (default)", NULL },
    { "nobest", '\0', 0, G_OPTION_ARG_NONE, &opt_nobest, "Fall back to older candidates when the best one is not installable", NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_packages, "Target packages", "PACKAGE..." },
    { NULL }
  };

  g_option_context_add_main_entries (opt_ctx, entries, NULL);
  if (!g_option_context_parse (opt_ctx, &argc, &argv, error))
    return FALSE;

  if (!opt_packages || !opt_packages[0])
    {
      g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                           "No packages specified for lockfile generation");
      return FALSE;
    }

  if (opt_best && opt_nobest)
    {
      g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "Argument --nobest is not allowed with argument --best");
      return FALSE;
    }
  const gboolean use_best = lock_use_best (opt_best, opt_nobest);
  const gboolean weak_deps = dnf_context_get_install_weak_deps ();

  /* dnf excludepkgs: applied by libdnf while the repos are added to the sack */
  g_auto(GStrv) excludepkgs = split_option_values (opt_excludepkgs);
  for (char **ex = excludepkgs; *ex; ++ex)
    {
      if (!dnf_conf_main_set_option ("excludepkgs", DNF_CONF_COMMANDLINE, *ex, error))
        return FALSE;
    }

  /* Without an install root, resolve against the repos alone so the lockfile
   * carries the complete closure. With --installroot, load its rpmdb so the
   * goal behaves like `dnf install` inside that root (module state is always
   * read from <installroot>/etc/dnf/modules.d by libdnf). */
  const gchar *install_root = dnf_context_get_install_root (ctx);
  const gboolean use_rpmdb = install_root && g_strcmp0 (install_root, "/") != 0 &&
                             installroot_has_rpmdb (install_root);
  if (use_rpmdb)
    {
      g_autofree gchar *usr_dir = g_build_filename (install_root, "usr", NULL);
      if (!g_file_test (usr_dir, G_FILE_TEST_IS_DIR))
        {
          g_set_error (error, DNF_ERROR, DNF_ERROR_FAILED,
                       "Install root %s has no usr/ directory; libdnf would ignore its rpmdb", install_root);
          return FALSE;
        }
    }
  else if (install_root && g_strcmp0 (install_root, "/") != 0)
    {
      g_autofree gchar *usr_dir = g_build_filename (install_root, "usr", NULL);
      if (g_file_test (usr_dir, G_FILE_TEST_IS_DIR))
        g_printerr ("warning: install root %s has no rpmdb (looked for "
                    "var/lib/rpm/{rpmdb.sqlite,Packages.db,Packages} and "
                    "usr/lib/sysimage/rpm/{rpmdb.sqlite,Packages.db,Packages}); "
                    "treating it as an empty image\n", install_root);
    }
  if (install_root && g_strcmp0 (install_root, "/") != 0 &&
      !dnf_context_get_platform_module (ctx) && !installroot_has_os_release (install_root))
    g_printerr ("warning: install root %s has no etc/os-release or usr/lib/os-release, so "
                "PLATFORM_ID is unknown and module streams may not resolve; set "
                "--setopt=module_platform_id=platform:<id>\n", install_root);

  DnfState *state = dnf_context_get_state (ctx);
  if (!dnf_context_setup_sack_with_flags (ctx, state,
                                          use_rpmdb ? DNF_CONTEXT_SETUP_SACK_FLAG_NONE
                                                    : DNF_CONTEXT_SETUP_SACK_FLAG_SKIP_RPMDB,
                                          error))
    return FALSE;

  DnfSack *sack = dnf_context_get_sack (ctx);

  g_auto(GStrv) module_specs = split_option_values (opt_module_enable);
  if (module_specs[0])
    {
      if (!dnf_context_module_enable (ctx, (const char **)module_specs, error))
        return FALSE;
      /* libdnf enables the streams' module dependencies after filtering; refresh. */
      if (!lock_recompute_modular_filtering (ctx, error))
        return FALSE;
    }

  hy_autoquery HyQuery query = get_packages_query (ctx, opt_packages, opt_archlist);
  g_autoptr(GPtrArray) target_pkgs = hy_query_run (query);

  /* Resolve dependency closure. By default each request is pinned to its
   * newest candidate. --nobest lets the solver pick any version of the
   * requested name/arch, preferring the newest. On top of an install root,
   * requests that are already installed (same or newer) are no-ops. */
  HyGoal goal = hy_goal_create (sack);
  for (char **spec = opt_packages; *spec; ++spec)
    {
      hy_autoquery HyQuery latest_query = get_spec_query (sack, *spec, TRUE, opt_archlist);
      g_autoptr(GPtrArray) spec_pkgs = hy_query_run (latest_query);
      const gboolean pinned = lock_spec_pins_version (sack, *spec);

      /* Like `dnf install`, a request with no candidate is an error, not a silent no-op. */
      if (spec_pkgs->len == 0 && !(use_rpmdb && spec_is_installed (sack, *spec)))
        {
          g_set_error (error, DNF_ERROR, DNF_ERROR_PACKAGE_NOT_FOUND,
                       "No match for argument: %s", *spec);
          hy_goal_free (goal);
          return FALSE;
        }

      g_autoptr(GPtrArray) wanted = g_ptr_array_new ();
      for (guint i = 0; i < spec_pkgs->len; ++i)
        {
          DnfPackage *pkg = g_ptr_array_index (spec_pkgs, i);
          if (!(use_rpmdb && is_satisfied_by_installed (sack, pkg, pinned)))
            g_ptr_array_add (wanted, pkg);
        }
      if (wanted->len == 0)
        continue;

      if (use_best)
        {
          /* One job per NEVRA, like dnf: a build carried by two repos (a snapshot
           * and a mirror) would otherwise be two conflicting installs. The
           * selector lets the solver pick the copy. */
          g_autoptr(GHashTable) by_nevra = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                                                  (GDestroyNotify)dnf_packageset_free);
          g_autoptr(GPtrArray) nevras = g_ptr_array_new ();
          for (guint i = 0; i < wanted->len; ++i)
            {
              DnfPackage *pkg = g_ptr_array_index (wanted, i);
              const gchar *nevra = dnf_package_get_nevra (pkg);
              DnfPackageSet *copies = g_hash_table_lookup (by_nevra, nevra);
              if (!copies)
                {
                  copies = dnf_packageset_new (sack);
                  g_hash_table_insert (by_nevra, (gpointer)nevra, copies);
                  g_ptr_array_add (nevras, (gpointer)nevra);
                }
              dnf_packageset_add (copies, pkg);
            }
          for (guint i = 0; i < nevras->len; ++i)
            {
              HySelector sltr = hy_selector_create (sack);
              hy_selector_pkg_set (sltr, g_hash_table_lookup (by_nevra, g_ptr_array_index (nevras, i)));
              gboolean ok = hy_goal_install_selector (goal, sltr, error);
              hy_selector_free (sltr);
              if (!ok)
                {
                  hy_goal_free (goal);
                  return FALSE;
                }
            }
          continue;
        }

      /* --nobest: let the solver pick among the candidates of the request. The
       * selector is built from the spec itself, so a pinned version, release or
       * arch stays a constraint; only unconstrained parts may fall back. */
      hy_autoquery HyQuery all_query = get_spec_query (sack, *spec, FALSE, opt_archlist);
      HySelector sltr = hy_selector_create (sack);
      DnfPackageSet *pset = hy_query_run_set (all_query);
      hy_selector_pkg_set (sltr, pset);
      dnf_packageset_free (pset);
      gboolean ok = hy_goal_install_selector (goal, sltr, error);
      hy_selector_free (sltr);
      if (!ok)
        {
          hy_goal_free (goal);
          return FALSE;
        }
    }

  if (weak_deps)
    {
      enum DnfConfPriority prio;
      g_autofree gchar *autodetect = dnf_conf_main_get_option ("exclude_from_weak_autodetect", &prio, NULL);
      g_autofree gchar *weak_excl = dnf_conf_main_get_option ("exclude_from_weak", &prio, NULL);
      g_auto(GStrv) weak_excl_v = g_strsplit_set (weak_excl ? weak_excl : "", ", ", -1);
      gboolean autodetect_on = TRUE;
      if (!lock_parse_bool ("exclude_from_weak_autodetect", autodetect, TRUE, &autodetect_on, error))
        {
          hy_goal_free (goal);
          return FALSE;
        }
      lock_goal_exclude_from_weak (goal, sack, autodetect_on, (const gchar * const *)weak_excl_v);
    }

  int goal_flags = DNF_NONE;
  if (use_best)
    goal_flags |= DNF_FORCE_BEST;
  if (!weak_deps)
    goal_flags |= DNF_IGNORE_WEAK_DEPS;

  if (hy_goal_run_flags (goal, goal_flags) != 0)
    {
      g_autofree gchar *problems = lock_goal_describe_problems (goal);
      g_set_error (error, DNF_ERROR, DNF_ERROR_NO_SOLUTION,
                   "Error resolving package dependencies%s%s",
                   problems && *problems ? ":\n" : "", problems ? problems : "");
      hy_goal_free (goal);
      return FALSE;
    }

  g_autoptr(GPtrArray) all_pkgs = g_ptr_array_new_with_free_func ((GDestroyNotify)g_object_unref);
  if (use_best && !use_rpmdb)
    ptr_array_add_refs (all_pkgs, target_pkgs);
  ptr_array_extend_and_steal (all_pkgs, hy_goal_list_installs (goal, NULL));
  /* Upgrades and downgrades replace an installed package of the same name. */
  g_autoptr(GPtrArray) upgrades = g_ptr_array_new_with_free_func ((GDestroyNotify)g_object_unref);
  g_autoptr(GPtrArray) upgrades_list = hy_goal_list_upgrades (goal, NULL);
  g_autoptr(GPtrArray) downgrades_list = hy_goal_list_downgrades (goal, NULL);
  ptr_array_add_refs (upgrades, upgrades_list);
  ptr_array_add_refs (upgrades, downgrades_list);
  ptr_array_add_refs (all_pkgs, upgrades);

  /* Installed packages the transaction replaces or removes (install root only) */
  g_autoptr(GHashTable) replaces = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                          (GDestroyNotify)g_ptr_array_unref);
  g_autoptr(GPtrArray) removed = g_ptr_array_new_with_free_func ((GDestroyNotify)g_object_unref);
  if (use_rpmdb)
    {
      for (guint i = 0; i < all_pkgs->len; ++i)
        {
          DnfPackage *pkg = g_ptr_array_index (all_pkgs, i);
          GPtrArray *old = hy_goal_list_obsoleted_by_package (goal, pkg);
          if (old && old->len > 0)
            g_hash_table_replace (replaces, g_strdup (dnf_package_get_nevra (pkg)), old);
          else if (old)
            g_ptr_array_unref (old);
        }
      g_autoptr(GPtrArray) erasures = hy_goal_list_erasures (goal, NULL);
      g_autoptr(GPtrArray) obsoleted = hy_goal_list_obsoleted (goal, NULL);
      ptr_array_add_refs (removed, erasures);
      ptr_array_add_refs (removed, obsoleted);
    }
  hy_goal_free (goal);

  DnfRepoLoader *repo_loader = dnf_context_get_repo_loader (ctx);
  g_autoptr(GPtrArray) resolved = select_one_pkg_for_nevra (repo_loader, all_pkgs);

  /* Names of the resolved packages */
  g_autoptr(GHashTable) resolved_names = g_hash_table_new (g_str_hash, g_str_equal);
  for (guint i = 0; i < resolved->len; ++i)
    {
      const char *name = dnf_package_get_name (g_ptr_array_index (resolved, i));
      if (name)
        g_hash_table_add (resolved_names, (gpointer)name);
    }

  /* Identify directly requested package names */
  g_autoptr(GHashTable) directly_requested = string_set_new ();
  for (guint i = 0; i < target_pkgs->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (target_pkgs, i);
      const char *name = dnf_package_get_name (pkg);
      if (name)
        g_hash_table_add (directly_requested, g_strdup (name));
    }

  /* Parse exclude package names */
  g_autoptr(GHashTable) exclude_set = string_set_new ();
  g_auto(GStrv) exclude_names = split_option_values (opt_exclude);
  for (char **ex = exclude_names; *ex; ++ex)
    g_hash_table_add (exclude_set, g_strdup (*ex));

  /* Discover direct dependencies of each resolved package */
  g_autoptr(DnfPackageSet) resolved_set = dnf_packageset_new (sack);
  for (guint i = 0; i < resolved->len; ++i)
    dnf_packageset_add (resolved_set, g_ptr_array_index (resolved, i));
  hy_autoquery HyQuery dep_query = hy_query_create (sack);
  hy_query_filter_package_in (dep_query, HY_PKG, HY_EQ, resolved_set);
  hy_query_apply (dep_query);
  g_autoptr(GHashTable) pkg_direct_deps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);
  g_autoptr(GHashTable) pkg_direct_nevras = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);
  /* Weak edges (Recommends, reverse Supplements): used to keep weak deps the
   * solver installed when pruning, not reported as "dependencies". */
  g_autoptr(GHashTable) pkg_weak_deps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);

  for (guint i = 0; i < resolved->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (resolved, i);
      const char *pkg_name = dnf_package_get_name (pkg);
      if (!pkg_name)
        continue;

      GHashTable *dep_names = string_set_new ();
      GHashTable *dep_nevras = string_set_new ();
      add_reldep_edges (dep_query, dnf_package_get_requires (pkg), pkg_name, dep_names, dep_nevras);
      g_hash_table_insert (pkg_direct_deps, g_strdup (pkg_name), dep_names);
      g_hash_table_insert (pkg_direct_nevras, g_strdup (pkg_name), dep_nevras);

      if (!weak_deps)
        continue;
      add_reldep_edges (dep_query, dnf_package_get_recommends (pkg), pkg_name,
                        lookup_or_new_set (pkg_weak_deps, pkg_name), NULL);
      /* pkg Supplements X: edge X -> pkg */
      g_autoptr(GHashTable) supplemented = string_set_new ();
      add_reldep_edges (dep_query, dnf_package_get_supplements (pkg), pkg_name, supplemented, NULL);
      GHashTableIter sup_it;
      gpointer sup_name;
      g_hash_table_iter_init (&sup_it, supplemented);
      while (g_hash_table_iter_next (&sup_it, &sup_name, NULL))
        g_hash_table_add (lookup_or_new_set (pkg_weak_deps, sup_name), g_strdup (pkg_name));
    }

  /* Excluded packages: those in exclude_set UNLESS directly requested */
  g_autoptr(GHashTable) excluded_pkgs = string_set_new ();
  GHashTableIter ex_iter;
  gpointer ex_key;
  g_hash_table_iter_init (&ex_iter, exclude_set);
  while (g_hash_table_iter_next (&ex_iter, &ex_key, NULL))
    {
      if (!g_hash_table_contains (directly_requested, ex_key))
        g_hash_table_add (excluded_pkgs, g_strdup ((const char *)ex_key));
    }

  /* Graph reachability: packages reachable from directly_requested without going through excluded_pkgs */
  g_autoptr(GHashTable) reachable = string_set_new ();
  GQueue *queue = g_queue_new ();

  GHashTableIter req_iter;
  gpointer req_key;
  g_hash_table_iter_init (&req_iter, directly_requested);
  while (g_hash_table_iter_next (&req_iter, &req_key, NULL))
    {
      if (g_hash_table_contains (resolved_names, req_key))
        mark_reachable (reachable, queue, req_key);
    }
  /* Upgrades and downgrades of installed packages are needed for the transaction even when
   * no new package requires them (e.g. conflicts). */
  for (guint i = 0; upgrades && i < upgrades->len; ++i)
    {
      const char *name = dnf_package_get_name (g_ptr_array_index (upgrades, i));
      if (name && !g_hash_table_contains (excluded_pkgs, name))
        mark_reachable (reachable, queue, name);
    }
  /* Packages the installed set requires, e.g. glibc's
   * "(glibc-gconv-extra if redhat-rpm-config)" once the transaction brings
   * redhat-rpm-config. Installed packages being upgraded or removed are
   * skipped; their replacements are roots above or gone. */
  if (use_rpmdb)
    {
      g_autoptr(GHashTable) gone_names = string_set_new ();
      g_autoptr(GHashTable) gone_nevras = string_set_new ();
      for (guint i = 0; upgrades && i < upgrades->len; ++i)
        g_hash_table_add (gone_names, g_strdup (dnf_package_get_name (g_ptr_array_index (upgrades, i))));
      for (guint i = 0; i < removed->len; ++i)
        g_hash_table_add (gone_nevras, g_strdup (dnf_package_get_nevra (g_ptr_array_index (removed, i))));

      hy_autoquery HyQuery installed_query = hy_query_create (sack);
      hy_query_filter (installed_query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
      g_autoptr(GPtrArray) installed = hy_query_run (installed_query);
      g_autoptr(GHashTable) installed_deps = string_set_new ();
      for (guint i = 0; i < installed->len; ++i)
        {
          DnfPackage *pkg = g_ptr_array_index (installed, i);
          if (g_hash_table_contains (gone_names, dnf_package_get_name (pkg)) ||
              g_hash_table_contains (gone_nevras, dnf_package_get_nevra (pkg)))
            continue;
          add_reldep_edges (dep_query, dnf_package_get_requires (pkg),
                            dnf_package_get_name (pkg), installed_deps, NULL);
        }
      GHashTableIter inst_it;
      gpointer inst_name;
      g_hash_table_iter_init (&inst_it, installed_deps);
      while (g_hash_table_iter_next (&inst_it, &inst_name, NULL))
        {
          if (!g_hash_table_contains (excluded_pkgs, inst_name))
            mark_reachable (reachable, queue, inst_name);
        }
    }

  while (!g_queue_is_empty (queue))
    {
      gchar *curr = g_queue_pop_head (queue);
      GHashTable *edge_sets[] = {
        g_hash_table_lookup (pkg_direct_deps, curr),
        g_hash_table_lookup (pkg_weak_deps, curr),
      };
      for (guint e = 0; e < G_N_ELEMENTS (edge_sets); ++e)
        {
          if (!edge_sets[e])
            continue;
          GHashTableIter dep_it;
          gpointer dep_k;
          g_hash_table_iter_init (&dep_it, edge_sets[e]);
          while (g_hash_table_iter_next (&dep_it, &dep_k, NULL))
            {
              const char *dep_name = dep_k;
              if (g_hash_table_contains (excluded_pkgs, dep_name))
                continue; /* Excluded: trim package and its exclusive dependencies */

              mark_reachable (reachable, queue, dep_name);
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
  json_object_object_add (root, "requested", sorted_set_array (directly_requested, NULL, NULL));

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

      /* Extract checksum and calculate SRI integrity string */
      int chk_type = 0;
      const unsigned char *chk_bytes = dnf_package_get_chksum (pkg, &chk_type);
      g_autofree gchar *integrity = NULL;
      g_autofree gchar *hex_chksum = NULL;

      if (chk_bytes)
        {
          int chk_len = 32; /* sha256, also for unknown types */
          const char *algo_name = "sha256";
          switch (chk_type)
            {
            case G_CHECKSUM_SHA512: chk_len = 64; algo_name = "sha512"; break;
            case G_CHECKSUM_SHA384: chk_len = 48; algo_name = "sha384"; break;
            case G_CHECKSUM_SHA1:   chk_len = 20; algo_name = "sha1"; break;
            case G_CHECKSUM_MD5:    chk_len = 16; algo_name = "md5"; break;
            default: break;
            }

          gchar *b64 = g_base64_encode (chk_bytes, chk_len);
          integrity = g_strdup_printf ("%s-%s", algo_name, b64);
          g_free (b64);

          GString *hex = g_string_sized_new (chk_len * 2);
          for (int b = 0; b < chk_len; ++b)
            g_string_append_printf (hex, "%02x", chk_bytes[b]);
          hex_chksum = g_string_free (hex, FALSE);
        }

      /* Compute direct and transitive dependencies among kept reachable packages */
      GHashTable *raw_deps = g_hash_table_lookup (pkg_direct_deps, name);
      GHashTable *raw_nevras = g_hash_table_lookup (pkg_direct_nevras, name);

      struct json_object *pkg_obj = json_object_new_object ();
      json_object_object_add (pkg_obj, "name", json_object_new_string (name));
      json_object_object_add (pkg_obj, "version", json_object_new_string (version ? version : ""));
      json_object_object_add (pkg_obj, "arch", json_object_new_string (arch ? arch : ""));
      json_object_object_add (pkg_obj, "nevra", json_object_new_string (nevra ? nevra : ""));
      json_object_object_add (pkg_obj, "url", json_object_new_string (url ? url : ""));
      json_object_object_add (pkg_obj, "integrity", json_object_new_string (integrity ? integrity : ""));
      if (hex_chksum)
        json_object_object_add (pkg_obj, "checksum", json_object_new_string (hex_chksum));

      gboolean is_req = g_hash_table_contains (directly_requested, name);
      json_object_object_add (pkg_obj, "requested", json_object_new_boolean (is_req));

      GPtrArray *replaced = g_hash_table_lookup (replaces, nevra);
      if (replaced)
        json_object_object_add (pkg_obj, "replaces", sorted_nevra_array (replaced));

      /* Direct dependencies */
      json_object_object_add (pkg_obj, "dependencies", sorted_set_array (raw_deps, reachable, NULL));

      /* Weak edges (Recommends, reverse Supplements) to kept packages, so the
       * exported graph reaches everything the solver installed. */
      json_object_object_add (pkg_obj, "weak_dependencies",
                              sorted_set_array (g_hash_table_lookup (pkg_weak_deps, name), reachable, name));

      json_object_object_add (pkg_obj, "dependencies_nevra", sorted_set_array (raw_nevras, NULL, NULL));

      /* Compute flat transitive dependencies */
      g_autoptr(GHashTable) trans_set = string_set_new ();
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
                      g_hash_table_contains (reachable, dep_n) &&
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

      json_object_object_add (pkg_obj, "transitive_dependencies", sorted_set_array (trans_set, NULL, NULL));

      json_object_array_add (packages_arr, pkg_obj);
    }

  json_object_object_add (root, "packages", packages_arr);
  if (removed->len > 0)
    json_object_object_add (root, "removed", sorted_nevra_array (removed));

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
