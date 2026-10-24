/* dnf/lock-repo.cpp
 *
 * C wrapper over libdnf's C++-only repo attachment (see lock-repo.h).
 */

#include "lock-repo.h"

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
