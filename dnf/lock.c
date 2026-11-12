/* dnf/lock.c
 *
 * Implements dependency resolution and lockfile generation in JSON.
 */

#include "lock.h"
#include "lock-repo.h"
#include "dnf-goal.h"
#include "dnf-utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <json-c/json.h>
#include <librepo/librepo.h>
#include <solv/evr.h>
#include <solv/pool.h>
#include <solv/pool_parserpmrichdep.h>
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
  GPtrArray *recommends;            /* gchar*, dependency strings */
  GPtrArray *supplements;           /* gchar*, dependency strings */
} InstalledPkg;

/* The strings of the array under @key of @obj, or NULL when there is no such array. */
static GPtrArray *
json_string_array (struct json_object *obj, const char *key)
{
  struct json_object *arr = NULL;
  if (!json_object_object_get_ex (obj, key, &arr) || !json_object_is_type (arr, json_type_array))
    return NULL;
  GPtrArray *out = g_ptr_array_new_with_free_func (g_free);
  int n = json_object_array_length (arr);
  for (int i = 0; i < n; ++i)
    {
      struct json_object *item = json_object_array_get_idx (arr, i);
      if (item)
        g_ptr_array_add (out, g_strdup (json_object_get_string (item)));
    }
  return out;
}

/* The dependency @str as pool_dep2str writes it: a rich dependency in parentheses, or "name" /
 * "name <op> evr". 0 when it is malformed. */
static Id
parse_dep (Pool *pool, const char *str)
{
  if (*str == '(')
    return pool_parserpmrichdep (pool, str);

  g_auto(GStrv) parts = g_strsplit (str, " ", 3);
  if (!parts[0] || !*parts[0])
    return 0;
  Id name = pool_str2id (pool, parts[0], 1);
  if (!parts[1])
    return name;
  int flags = 0;
  for (const char *c = parts[1]; *c; ++c)
    {
      if (*c == '<')
        flags |= REL_LT;
      else if (*c == '=')
        flags |= REL_EQ;
      else if (*c == '>')
        flags |= REL_GT;
      else
        return 0;
    }
  if (!flags || !parts[2] || !*parts[2])
    return 0;
  return pool_rel2id (pool, name, pool_str2id (pool, parts[2], 1), flags, 1);
}

/* Adds the dependency strings @deps to @new_id under @keyname. */
static void
add_dep_strings (Pool *pool, Repo *sys_repo, Id new_id, Id keyname, const GPtrArray *deps)
{
  for (guint i = 0; deps && i < deps->len; ++i)
    {
      Id dep = parse_dep (pool, g_ptr_array_index (deps, i));
      if (dep)
        repo_add_deparray (sys_repo, new_id, keyname, dep, 0);
    }
}

/* Records the dependency array @off of @s under @key of @pkg_obj, as strings; nothing when it is empty. */
static void
add_dep_strings_to_json (struct json_object *pkg_obj, const char *key, Pool *pool, const Solvable *s, Offset off)
{
  if (!off)
    return;
  struct json_object *arr = json_object_new_array ();
  for (Id *dp = s->repo->idarraydata + off; *dp; dp++)
    json_object_array_add (arr, json_object_new_string (pool_dep2str (pool, *dp)));
  json_object_object_add (pkg_obj, key, arr);
}

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
  if (pkg->recommends)
    g_ptr_array_unref (pkg->recommends);
  if (pkg->supplements)
    g_ptr_array_unref (pkg->supplements);
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
compute_package_integrity (DnfPackage *pkg);

/* TRUE if the layer's recorded integrity says @pkg is a different build. A missing or
 * empty integrity on either side says nothing, so it counts as the same build. */
static gboolean
layer_integrity_differs (const gchar *layer_integrity, DnfPackage *pkg)
{
  if (!layer_integrity || !*layer_integrity)
    return FALSE;
  g_autofree gchar *integrity = compute_package_integrity (pkg);
  return integrity && *integrity && g_strcmp0 (layer_integrity, integrity) != 0;
}

/* Copies the dependency array @from_off of @from, under @keyname, onto @new_id. */
static void
copy_deparray (Repo *sys_repo, Id new_id, const Solvable *from, Id keyname, Offset from_off)
{
  if (!from_off)
    return;
  for (Id *dp = from->repo->idarraydata + from_off; *dp; dp++)
    repo_add_deparray (sys_repo, new_id, keyname, *dp, 0);
}

/* The layer holds @inst at a NEVRA the repositories lack, so its own provides are unknown.
 * Give the synthetic solvable @new_id those of the repositories' newest build of the name:
 * sonames and file paths rarely change between builds, and a solvable that provides only its
 * name leaves the layer's other packages, whose requires come from the repositories, unsatisfied.
 * The build's provides on the name itself (name, name = evr, name(arch) = evr) are its own and
 * are skipped. */
static void
add_provides_of_name (Pool *pool, Repo *sys_repo, Id new_id, DnfSack *sack, const InstalledPkg *inst,
                      const gchar *opt_archlist)
{
  hy_autoquery HyQuery q = hy_query_create (sack);
  hy_query_filter (q, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);
  hy_query_filter (q, HY_PKG_NAME, HY_EQ, inst->name);
  if (inst->arch)
    hy_query_filter (q, HY_PKG_ARCH, HY_EQ, inst->arch);
  else if (opt_archlist)
    {
      g_auto(GStrv) archs = g_strsplit_set (opt_archlist, ", ", -1);
      hy_query_filter_in (q, HY_PKG_ARCH, HY_EQ, (const char **)archs);
    }
  hy_query_filter_num (q, HY_PKG_LATEST_PER_ARCH, HY_EQ, 1);
  g_autoptr(GPtrArray) builds = hy_query_run (q);
  if (!builds || builds->len == 0)
    return;

  Solvable *s_build = pool_id2solvable (pool, dnf_package_get_id (g_ptr_array_index (builds, 0)));
  if (!s_build->provides)
    return;
  const char *name = pool_id2str (pool, s_build->name);
  gsize name_len = strlen (name);
  for (Id *dp = s_build->repo->idarraydata + s_build->provides; *dp; dp++)
    {
      Id base = *dp;
      while (ISRELDEP (base))
        base = GETRELDEP (pool, base)->name;
      const char *base_str = pool_id2str (pool, base);
      if (strncmp (base_str, name, name_len) == 0 && (base_str[name_len] == '\0' || base_str[name_len] == '('))
        continue;
      repo_add_deparray (sys_repo, new_id, SOLVABLE_PROVIDES, *dp, 0);
    }
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

/* Copies member @key of package @i of lockfile @path into *@out: a string, or
 * NULL when the member is absent or null. */
static gboolean
lock_string_member (const gchar *path, int i, struct json_object *obj, const char *key, gchar **out, GError **error)
{
  struct json_object *value = NULL;
  if (!json_object_object_get_ex (obj, key, &value) || json_object_is_type (value, json_type_null))
    return TRUE;
  if (!json_object_is_type (value, json_type_string))
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Lockfile '%s': package %d has a non-string %s", path, i, key);
      return FALSE;
    }
  *out = g_strdup (json_object_get_string (value));
  return TRUE;
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
            {
              g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                           "Lockfile '%s': package %d is not an object", path, i);
              json_object_put (root);
              return FALSE;
            }

          InstalledPkg *pkg = g_new0 (InstalledPkg, 1);
          if (!lock_string_member (path, i, p_obj, "name", &pkg->name, error) ||
              !lock_string_member (path, i, p_obj, "version", &pkg->version, error) ||
              !lock_string_member (path, i, p_obj, "arch", &pkg->arch, error) ||
              !lock_string_member (path, i, p_obj, "nevra", &pkg->nevra, error) ||
              !lock_string_member (path, i, p_obj, "url", &pkg->url, error) ||
              !lock_string_member (path, i, p_obj, "integrity", &pkg->integrity, error) ||
              !lock_string_member (path, i, p_obj, "checksum", &pkg->checksum, error))
            {
              installed_pkg_free (pkg);
              json_object_put (root);
              return FALSE;
            }
          if (!pkg->name || !*pkg->name)
            {
              g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                           "Lockfile '%s': package %d has no name", path, i);
              installed_pkg_free (pkg);
              json_object_put (root);
              return FALSE;
            }
          const char *name = pkg->name;

          struct json_object *req_obj = NULL;
          if (json_object_object_get_ex (p_obj, "requested", &req_obj))
            pkg->requested = json_object_get_boolean (req_obj);

          pkg->dependencies = json_string_array (p_obj, "dependencies");
          pkg->dependencies_nevra = json_string_array (p_obj, "dependencies_nevra");
          pkg->transitive_deps = json_string_array (p_obj, "transitive_dependencies");
          pkg->recommends = json_string_array (p_obj, "recommends");
          pkg->supplements = json_string_array (p_obj, "supplements");

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

/* The candidates of the specs, one per name.arch. Repos of equal priority each
 * contribute their newest package, so the same NEVRA can appear once per repo
 * (or an older and a newer one); installing every one of them fails with
 * "cannot install both X from repoA and X from repoB". Keep the highest EVR and,
 * for equal EVRs, the repo that wins on priority and cost. */
static GPtrArray *
pick_one_pkg_per_name_arch (DnfRepoLoader *repo_loader, GPtrArray *pkgs)
{
  g_autoptr(GHashTable) best = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  for (guint i = 0; i < pkgs->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (pkgs, i);
      g_autofree gchar *key = g_strdup_printf ("%s.%s", dnf_package_get_name (pkg),
                                               dnf_package_get_arch (pkg));
      DnfPackage *cur = g_hash_table_lookup (best, key);
      if (!cur)
        {
          g_hash_table_insert (best, g_steal_pointer (&key), pkg);
          continue;
        }
      gint cmp = dnf_package_evr_cmp (pkg, cur);
      if (cmp > 0 || (cmp == 0 && dnf_package_repo_prio_cost_cmp (pkg, cur, repo_loader) < 0))
        g_hash_table_insert (best, g_steal_pointer (&key), pkg);
    }

  GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify)g_object_unref);
  for (guint i = 0; i < pkgs->len; ++i)
    {
      DnfPackage *pkg = g_ptr_array_index (pkgs, i);
      g_autofree gchar *key = g_strdup_printf ("%s.%s", dnf_package_get_name (pkg),
                                               dnf_package_get_arch (pkg));
      if (g_hash_table_lookup (best, key) == pkg)
        g_ptr_array_add (out, g_object_ref (pkg));
    }
  return out;
}

/* Records the weak-dependency edges of @pkg_name in @edges (name -> set of names):
 * from the packages its @deps (Recommends) name to it, or with @reverse
 * (Supplements) from them to it, limited to the packages of the solve. (libsolv
 * does not pull a package in for a Supplements trigger a layer below installed,
 * so such a trigger needs no edge.)
 * Rich dependencies need no parsing: the provides query of a boolean dependency
 * returns the providers of every operand ("(foo and bar)" matches foo and bar,
 * "(a if b)" matches a), so an edge can be added that the solver would not
 * follow, never one fewer. */
static void
collect_weak_edges (HyQuery dep_query, DnfReldepList *deps, GHashTable *id_to_pkg,
                    const char *pkg_name, gboolean reverse, GHashTable *edges)
{
  if (!deps)
    return;

  int n_deps = dnf_reldep_list_count (deps);
  for (int d = 0; d < n_deps; ++d)
    {
      DnfReldep *reldep = dnf_reldep_list_index (deps, d);
      hy_query_filter_reldep (dep_query, HY_PKG_PROVIDES, reldep);
      g_autoptr(GPtrArray) providers = hy_query_run (dep_query);
      hy_query_clear (dep_query);
      dnf_reldep_free (reldep);

      for (guint p = 0; p < providers->len; ++p)
        {
          DnfPackage *prov = g_ptr_array_index (providers, p);
          const char *prov_name = dnf_package_get_name (prov);
          if (!prov_name || g_strcmp0 (prov_name, pkg_name) == 0 ||
              !g_hash_table_contains (id_to_pkg, GUINT_TO_POINTER (dnf_package_get_id (prov))))
            continue;

          const char *from = reverse ? prov_name : pkg_name;
          const char *to = reverse ? pkg_name : prov_name;
          GHashTable *set = g_hash_table_lookup (edges, from);
          if (!set)
            {
              set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
              g_hash_table_insert (edges, g_strdup (from), set);
            }
          g_hash_table_add (set, g_strdup (to));
        }
    }
  dnf_reldep_list_free (deps);
}

/* Candidates of the specs in @pkgs_spec: just the newest per arch with @latest,
 * every version otherwise (for --nobest). */
static HyQuery
get_packages_query (DnfContext *ctx, GStrv pkgs_spec, const gchar *opt_archlist, gboolean latest)
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
          if (latest)
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

/* Splits repeated "a,b c" option values into a NULL-terminated, de-duplicated list. */
static GStrv
split_option_values (GStrv values)
{
  g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func (g_free);
  for (char **val = values; val && *val; ++val)
    {
      g_auto(GStrv) tokens = g_strsplit_set (*val, ", ", -1);
      for (char **tok = tokens; *tok; ++tok)
        {
          g_strstrip (*tok);
          if (**tok && !g_ptr_array_find_with_equal_func (out, *tok, g_str_equal, NULL))
            g_ptr_array_add (out, g_strdup (*tok));
        }
    }
  g_ptr_array_add (out, NULL);
  return (GStrv)g_ptr_array_free (g_steal_pointer (&out), FALSE);
}

/* dnf's "best" is opt-in in libdnf (default False) but every EL dnf.conf sets
 * best=True, and the lockfile has always pinned the newest candidate. Keep
 * that unless --nobest was given or "best" was set explicitly (config file,
 * --setopt=best=..., --best). */
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

gboolean
run_lock (int argc, char **argv, GOptionContext *opt_ctx, DnfContext *ctx, GError **error)
{
  g_autofree gchar *opt_output = NULL;
  g_autofree gchar *opt_archlist = NULL;
  g_autofree gchar *opt_forcearch = NULL;
  g_auto(GStrv) opt_exclude = NULL;
  g_auto(GStrv) opt_excludepkgs = NULL;
  g_auto(GStrv) opt_module_enable = NULL;
  g_auto(GStrv) opt_lockfiles = NULL;
  g_auto(GStrv) opt_packages = NULL;
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
    { "lockfile", 'l', 0, G_OPTION_ARG_FILENAME_ARRAY, &opt_lockfiles, "Ordered list of lockfiles from previous layers", "FILE" },
    { "lock-file", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &opt_lockfiles, "Ordered list of lockfiles from previous layers", "FILE" },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_packages, "Target packages", "PACKAGE..." },
    { NULL }
  };

  g_option_context_add_main_entries (opt_ctx, entries, NULL);
  if (!g_option_context_parse (opt_ctx, &argc, &argv, error))
    return FALSE;

  if (opt_best && opt_nobest)
    {
      g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "Argument --nobest is not allowed with argument --best");
      return FALSE;
    }
  const gboolean use_best = lock_use_best (opt_best, opt_nobest);

  /* dnf excludepkgs: applied by libdnf while the repos are added to the sack */
  g_auto(GStrv) excludepkgs = split_option_values (opt_excludepkgs);
  if (excludepkgs[0])
    {
      g_autofree gchar *globs = g_strjoinv (",", excludepkgs);
      if (!dnf_conf_main_set_option ("excludepkgs", DNF_CONF_COMMANDLINE, globs, error))
        return FALSE;
    }

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

  g_auto(GStrv) module_specs = split_option_values (opt_module_enable);
  if (module_specs[0])
    {
      if (!dnf_context_module_enable (ctx, (const char **)module_specs, error))
        return FALSE;
      /* libdnf enables the streams' module dependencies after filtering; refresh. */
      if (!lock_recompute_modular_filtering (ctx, error))
        return FALSE;
    }

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
              if (layer_integrity_differs (inst->integrity, cand))
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
              /* dnf leaves out the providers of Recommends an installed package left unmet */
              copy_deparray (sys_repo, new_id, s_cand, SOLVABLE_RECOMMENDS, s_cand->recommends);
              copy_deparray (sys_repo, new_id, s_cand, SOLVABLE_SUPPLEMENTS, s_cand->supplements);
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
              add_provides_of_name (pool, sys_repo, new_id, sack, inst, opt_archlist);

              if (inst->dependencies)
                {
                  for (guint d = 0; d < inst->dependencies->len; ++d)
                    {
                      const char *dn = g_ptr_array_index (inst->dependencies, d);
                      repo_add_deparray (sys_repo, new_id, SOLVABLE_REQUIRES, pool_str2id (pool, dn, 1), 0);
                    }
                }
              /* Recorded by the lock that put the package in the layer: dnf leaves out the
               * providers of Recommends an installed package left unmet. */
              add_dep_strings (pool, sys_repo, new_id, SOLVABLE_RECOMMENDS, inst->recommends);
              add_dep_strings (pool, sys_repo, new_id, SOLVABLE_SUPPLEMENTS, inst->supplements);
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

  DnfRepoLoader *repo_loader = dnf_context_get_repo_loader (ctx);
  hy_autoquery HyQuery query = get_packages_query (ctx, pkgs_spec_array, opt_archlist, TRUE);
  g_autoptr(GPtrArray) candidate_pkgs = hy_query_run (query);
  g_autoptr(GPtrArray) target_pkgs = pick_one_pkg_per_name_arch (repo_loader, candidate_pkgs);

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
  g_autoptr(GPtrArray) obsoleted = NULL;

  /* dnf's install_weak_deps (default True): the solver installs the Recommends
   * of what it installs, and the Supplements that match */
  const gboolean weak_deps = dnf_context_get_install_weak_deps ();

  if (target_pkgs->len > 0)
    {
      HyGoal goal = hy_goal_create (sack);
      if (use_best)
        {
          for (guint i = 0; i < target_pkgs->len; ++i)
            {
              DnfPackage *pkg = g_ptr_array_index (target_pkgs, i);
              /* dnf leaves an installed package alone unless the repositories hold a newer
               * build: an install job for an older or identical copy of it would count as a
               * fresh install and pull in that copy's weak dependencies. */
              InstalledPkg *inst = lookup_installed (installed_by_key, pkg, installonly);
              if (inst && inst->version)
                {
                  int cmp = pool_evrcmp_str (pool, inst->version, dnf_package_get_evr (pkg), EVRCMP_COMPARE);
                  if (cmp > 0 || (cmp == 0 && !layer_integrity_differs (inst->integrity, pkg)))
                    continue;
                }
              hy_goal_install (goal, pkg);
            }
        }
      else
        {
          /* --nobest: let the solver pick among all candidates of each request,
           * preferring the newest. A pinned version or arch stays a constraint. */
          for (guint i = 0; i < actual_target_pkgs->len; ++i)
            {
              char *spec_array[] = { g_ptr_array_index (actual_target_pkgs, i), NULL };
              hy_autoquery HyQuery all_query = get_packages_query (ctx, spec_array, opt_archlist, FALSE);
              DnfPackageSet *pset = hy_query_run_set (all_query);
              gboolean ok = TRUE;
              if (dnf_packageset_count (pset) > 0)
                {
                  HySelector sltr = hy_selector_create (sack);
                  hy_selector_pkg_set (sltr, pset);
                  ok = hy_goal_install_selector (goal, sltr, error);
                  hy_selector_free (sltr);
                }
              dnf_packageset_free (pset);
              if (!ok)
                {
                  hy_goal_free (goal);
                  return FALSE;
                }
            }
        }

      int run_flags = DNF_NONE;
      if (use_best)
        run_flags |= DNF_FORCE_BEST;
      if (!weak_deps)
        run_flags |= DNF_IGNORE_WEAK_DEPS;

      /* dnf_goal_depsolve, unlike hy_goal_run_flags, excludes from weak-dependency
       * installation the providers of Recommends that the installed packages left
       * unmet, so an upgrade only adds the weak dependencies that are new. */
      g_autoptr(GError) depsolve_error = NULL;
      /* Every target may already be in the layers below: an empty transaction is no failure. */
      if (!dnf_goal_depsolve (goal, (DnfGoalActions)run_flags, &depsolve_error) &&
          !g_error_matches (depsolve_error, DNF_ERROR, DNF_ERROR_NO_PACKAGES_TO_UPDATE))
        {
          g_autofree gchar *problems = lock_goal_describe_problems (goal);
          g_set_error (error, DNF_ERROR, DNF_ERROR_NO_SOLUTION,
                       "Error resolving package dependencies%s%s",
                       problems && *problems ? ":\n" : "", problems ? problems : "");
          hy_goal_free (goal);
          return FALSE;
        }

      /* Only @System packages can be obsoleted */
      obsoleted = hy_goal_list_obsoleted (goal, NULL);
      for (guint i = 0; obsoleted && i < obsoleted->len; ++i)
        g_printerr ("warning: the solve obsoletes %s from a layer below\n",
                    dnf_package_get_nevra (g_ptr_array_index (obsoleted, i)));

      /* Include target packages in all_pkgs ONLY if new or integrity changed.
       * With --nobest the solver may pick another version; installs lists it. */
      for (guint i = 0; use_best && i < target_pkgs->len; ++i)
        {
          DnfPackage *pkg = g_ptr_array_index (target_pkgs, i);
          InstalledPkg *inst = lookup_installed (installed_by_key, pkg, installonly);
          if (!inst)
            {
              g_ptr_array_add (all_pkgs, g_object_ref (pkg));
            }
          else if (layer_integrity_differs (inst->integrity, pkg))
            g_ptr_array_add (all_pkgs, g_object_ref (pkg));
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
              else if (layer_integrity_differs (inst->integrity, pkg))
                g_ptr_array_add (all_pkgs, g_object_ref (pkg));
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

  /* Without --exclude every package the solver installs is kept, as dnf installs
   * it. With it, what is only reachable through an excluded package is trimmed. */
  const gboolean trim = g_hash_table_size (exclude_set) > 0;

  /* Weak edges (Recommends, reverse Supplements), only followed to find what to
   * trim; they stay out of "dependencies" as the package does not require them. */
  g_autoptr(GHashTable) pkg_weak_deps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);

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

      if (trim && weak_deps)
        {
          collect_weak_edges (dep_query, dnf_package_get_recommends (pkg), id_to_pkg,
                              pkg_name, FALSE, pkg_weak_deps);
          collect_weak_edges (dep_query, dnf_package_get_supplements (pkg), id_to_pkg,
                              pkg_name, TRUE, pkg_weak_deps);
        }
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
  if (!trim || !any_in_reachable)
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

  if (obsoleted && obsoleted->len > 0)
    {
      struct json_object *obsoleted_arr = json_object_new_array ();
      for (guint i = 0; i < obsoleted->len; ++i)
        {
          DnfPackage *opkg = g_ptr_array_index (obsoleted, i);
          struct json_object *o_obj = json_object_new_object ();
          json_object_object_add (o_obj, "name", json_object_new_string (dnf_package_get_name (opkg)));
          const char *oevr = dnf_package_get_evr (opkg);
          if (oevr)
            json_object_object_add (o_obj, "version", json_object_new_string (oevr));
          const char *oarch = dnf_package_get_arch (opkg);
          if (oarch)
            json_object_object_add (o_obj, "arch", json_object_new_string (oarch));
          const char *onevra = dnf_package_get_nevra (opkg);
          if (onevra)
            json_object_object_add (o_obj, "nevra", json_object_new_string (onevra));
          json_object_array_add (obsoleted_arr, o_obj);
        }
      json_object_object_add (root, "obsoleted", obsoleted_arr);
    }

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

      /* Weak dependencies, so that a later lock on top of this one knows what the package left unmet */
      Solvable *s_pkg = pool_id2solvable (pool, dnf_package_get_id (pkg));
      add_dep_strings_to_json (pkg_obj, "recommends", pool, s_pkg, s_pkg->recommends);
      add_dep_strings_to_json (pkg_obj, "supplements", pool, s_pkg, s_pkg->supplements);

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
