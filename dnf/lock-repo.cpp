/* dnf/lock-repo.cpp
 *
 * C wrapper over libdnf's C++-only repo attachment (see lock-repo.h).
 */

#include "lock-repo.h"

#include <libdnf/dnf-sack-private.hpp>
#include <libdnf/hy-repo-private.hpp>
#include <libdnf/repo/Repo-private.hpp>
#include <libdnf/goal/Goal.hpp>

#include <exception>
#include <vector>

/* Same steps as dnf_sack_setup_cmdline_repo in libdnf's dnf-sack.cpp. */
extern "C" Repo *
lock_repo_create (Pool *pool, const char *name)
{
  HyRepo hrepo = hy_repo_create (name);
  auto repoImpl = libdnf::repoGetImpl (hrepo);
  Repo *repo = repo_create (pool, name);
  repo->appdata = hrepo;
  repoImpl->libsolvRepo = repo;
  repoImpl->needs_internalizing = 1;
  return repo;
}

/* Same as dnf_sack_add_cmdline_package does after adding a package. */
extern "C" void
lock_sack_repos_changed (DnfSack *sack)
{
  dnf_sack_set_provides_not_ready (sack);
  dnf_sack_set_considered_to_update (sack);
}

/* Same steps as dnf_context_module_enable does after enabling the streams. */
extern "C" gboolean
lock_recompute_modular_filtering (DnfContext *ctx, GError **error)
{
  DnfSack *sack = dnf_context_get_sack (ctx);
  auto container = dnf_sack_get_module_container (sack);
  if (!container)
    return TRUE;

  /* Don't filter RPMs from repos with module_hotfixes set, like libdnf does. */
  std::vector<const char *> hotfix_repos;
  GPtrArray *repos = dnf_context_get_repos (ctx);
  for (guint i = 0; repos && i < repos->len; i++)
    {
      auto repo = static_cast<DnfRepo *> (g_ptr_array_index (repos, i));
      if (dnf_repo_get_enabled (repo) != DNF_REPO_ENABLED_NONE && dnf_repo_get_module_hotfixes (repo))
        hotfix_repos.push_back (dnf_repo_get_id (repo));
    }
  hotfix_repos.push_back (nullptr);

  try
    {
      auto result = dnf_sack_filter_modules_v2 (sack, container, hotfix_repos.data (),
                                                nullptr, nullptr, true, false, false);
      if (result.second != libdnf::ModulePackageContainer::ModuleErrorType::NO_ERROR)
        {
          g_set_error (error, DNF_ERROR, DNF_ERROR_FAILED, "Modular dependency problems:\n%s",
                       libdnf::Goal::formatAllProblemRules (result.first).c_str ());
          return FALSE;
        }
    }
  catch (const std::exception &ex)
    {
      g_set_error (error, DNF_ERROR, DNF_ERROR_FAILED, "%s", ex.what ());
      return FALSE;
    }
  return TRUE;
}

extern "C" gchar *
lock_goal_describe_problems (HyGoal goal)
{
  try
    {
      auto problems = goal->describeAllProblemRules (true);
      if (problems.empty ())
        return g_strdup ("");
      return g_strdup (libdnf::Goal::formatAllProblemRules (problems).c_str ());
    }
  catch (const std::exception &ex)
    {
      return g_strdup (ex.what ());
    }
}
