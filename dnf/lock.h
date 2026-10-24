/* dnf/lock.h
 *
 * Header for lockfile generator.
 */

#pragma once

#include <glib.h>
#include <libdnf/libdnf.h>

G_BEGIN_DECLS

gboolean run_lock (int argc, char **argv, GOptionContext *opt_ctx, DnfContext *ctx, GError **error);

/* Parses a boolean option value like libdnf/microdnf (1/0, yes/no, true/false,
 * on/off, case-insensitive). An empty or NULL @value yields @default_value;
 * anything else unrecognised is an error. */
gboolean lock_parse_bool (const gchar *name, const gchar *value, gboolean default_value,
                          gboolean *out, GError **error);

G_END_DECLS
