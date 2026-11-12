/* dnf/solve-arch.h
 *
 * The architecture the standalone tools solve for, shared by dnf_lock and mark.
 */

#pragma once

#include <glib.h>

/* Maps a solve architecture to dnf's $basearch (i686 -> i386, armv7hl -> armhfp, ...). */
static inline const gchar *
basearch_for_arch (const gchar *arch)
{
  if (arch[0] == 'i' && arch[1] != '\0' && g_str_has_suffix (arch, "86") && strlen (arch) == 4)
    return "i386";
  if (g_str_has_prefix (arch, "armv") && g_str_has_suffix (arch, "hl"))
    return "armhfp";
  return arch;
}

/* The architecture to solve for: --forcearch, else the first non-noarch/src
 * --archlist entry. Read from a copy of argv, ahead of dnf_context_setup, so
 * that $basearch in repo URLs follows it too. */
static inline gchar *
find_solve_arch (int argc, char **argv)
{
  g_autofree gchar *opt_archlist = NULL;
  g_autofree gchar *opt_forcearch = NULL;
  const GOptionEntry entries[] = {
    { "archlist", '\0', 0, G_OPTION_ARG_STRING, &opt_archlist, NULL, NULL },
    { "forcearch", '\0', 0, G_OPTION_ARG_STRING, &opt_forcearch, NULL, NULL },
    { NULL }
  };

  g_autofree gchar **argv_copy = g_new0 (gchar *, argc + 1);
  for (int i = 0; i < argc; i++)
    argv_copy[i] = argv[i];
  int argc_copy = argc;
  gchar **argv_ptr = argv_copy;

  g_autoptr(GOptionContext) arch_ctx = g_option_context_new (NULL);
  g_option_context_add_main_entries (arch_ctx, entries, NULL);
  g_option_context_set_ignore_unknown_options (arch_ctx, TRUE);
  g_option_context_set_help_enabled (arch_ctx, FALSE);
  if (!g_option_context_parse (arch_ctx, &argc_copy, &argv_ptr, NULL))
    return NULL; /* run_lock reports the malformed option */

  if (opt_forcearch && *opt_forcearch)
    return g_steal_pointer (&opt_forcearch);
  if (opt_archlist)
    {
      g_auto(GStrv) archs = g_strsplit_set (opt_archlist, ", ", -1);
      for (char **a = archs; *a; ++a)
        {
          if (**a && g_strcmp0 (*a, "noarch") != 0 && g_strcmp0 (*a, "src") != 0)
            return g_strdup (*a);
        }
    }
  return NULL;
}
