/* dnf/lock-repo.h
 *
 * C access to libdnf's C++-only repo attachment, used by lock.c.
 */

#pragma once

#include <glib.h>
#include <solv/pool.h>
#include <solv/repo.h>

G_BEGIN_DECLS

/* Creates an empty libsolv repo named @name with a HyRepo attached both ways,
 * as libdnf does for its own repos. libdnf asserts that repo->appdata's HyRepo
 * points back at the repo whenever it internalizes, and frees the HyRepo with
 * the sack. */
Repo *lock_repo_create (Pool *pool, const char *name);

G_END_DECLS
