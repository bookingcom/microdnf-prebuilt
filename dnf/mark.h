/* dnf/plugins/mark/mark.h
 *
 * Header for mark command.
 */

#pragma once

#include <glib.h>
#include <libdnf/libdnf.h>

G_BEGIN_DECLS

gboolean run_mark (int argc, char **argv, GOptionContext *opt_ctx, DnfContext *ctx, GError **error);

G_END_DECLS
