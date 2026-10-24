/* dnf/lock-repo.cpp
 *
 * C wrapper over libdnf's C++-only repo attachment (see lock-repo.h).
 */

#include "lock-repo.h"

#include <libdnf/dnf-sack-private.hpp>
#include <libdnf/hy-repo-private.hpp>
#include <libdnf/repo/Repo-private.hpp>

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
