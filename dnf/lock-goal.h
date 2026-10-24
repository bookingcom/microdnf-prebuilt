/* dnf/lock-goal.h
 *
 * C wrappers around libdnf C++ APIs used by the lockfile generator that have
 * no C binding (weak-dependency exclusion, problem rendering, modular
 * filtering refresh).
 */

#pragma once

#include <glib.h>
#include <libdnf/libdnf.h>

G_BEGIN_DECLS

/* Mirrors dnf's exclude_from_weak and exclude_from_weak_autodetect handling:
 * available packages matching @patterns, plus (with @autodetect) weak deps
 * that the installed set already declined, are never pulled in as weak deps. */
void lock_goal_exclude_from_weak (HyGoal goal, DnfSack *sack, gboolean autodetect,
                                  const gchar * const *patterns);

/* Returns a newly allocated, human readable description of all solver problems. */
gchar *lock_goal_describe_problems (HyGoal goal);

/* Recomputes modular filtering after streams were enabled in the sack's module
 * container, without reloading module state from disk. */
gboolean lock_recompute_modular_filtering (DnfContext *ctx, GError **error);

/* Whether @spec (a NEVRA-like package spec) names a version or release, as in
 * `foo-1.0` or `foo-1.0-1.noarch`, rather than just a name or provide. */
gboolean lock_spec_pins_version (DnfSack *sack, const gchar *spec);

G_END_DECLS
