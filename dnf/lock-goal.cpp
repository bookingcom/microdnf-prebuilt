/* dnf/lock-goal.cpp
 *
 * C wrappers around libdnf C++ APIs used by the lockfile generator.
 */

#include "lock-goal.h"

#include <libdnf/dnf-sack-private.hpp>
#include <libdnf/goal/Goal.hpp>
#include <libdnf/sack/packageset.hpp>
#include <libdnf/sack/query.hpp>

#include <exception>
#include <string>
#include <vector>

void
lock_goal_exclude_from_weak (HyGoal goal, DnfSack *sack, gboolean autodetect,
                             const gchar * const *patterns)
{
  if (patterns)
    {
      for (const gchar * const *pattern = patterns; *pattern; ++pattern)
        {
          if (!**pattern)
            continue;
          libdnf::Query query (sack);
          auto ret = query.filterSubject (*pattern, nullptr, false, true, false, false);
          if (!ret.first)
            continue;
          query.available ();
          goal->add_exclude_from_weak (*query.getResultPset ());
        }
    }
  if (autodetect)
    goal->exclude_from_weak_autodetect ();
}

gboolean
lock_spec_pins_version (DnfSack *sack, const gchar *spec)
{
  try
    {
      libdnf::Query query (sack);
      auto ret = query.filterSubject (spec, nullptr, false, true, true, true);
      const auto &nevra = ret.second;
      return nevra && (!nevra->getVersion ().empty () || !nevra->getRelease ().empty ());
    }
  catch (const std::exception &)
    {
      return FALSE;
    }
}

gchar *
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

gboolean
lock_recompute_modular_filtering (DnfContext *ctx, GError **error)
{
  DnfSack *sack = dnf_context_get_sack (ctx);
  auto container = dnf_sack_get_module_container (sack);
  if (!container)
    return TRUE;

  /* Don't filter RPMs from repos with module_hotfixes set, like libdnf does. */
  std::vector<const char *> hotfix_repos;
  GPtrArray *repos = dnf_context_get_repos (ctx);
  for (guint i = 0; repos && i < repos->len; ++i)
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
