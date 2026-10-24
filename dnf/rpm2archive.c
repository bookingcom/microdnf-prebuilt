/* dnf/rpm2archive.c
 *
 * Implements multi-RPM archive generation with entry deduplication.
 * Compatible with rules_oci and rules_image layers.
 */

#include "rpm2archive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmstring.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmio.h>
#include <rpm/rpmts.h>

#include <archive.h>
#include <archive_entry.h>

#define BUFSIZE (128 * 1024)

static void
fill_archive_entry (struct archive_entry *entry, rpmfi fi)
{
  archive_entry_clear (entry);
  const char *dn = rpmfiDN (fi);
  if (!dn || !strcmp (dn, ""))
    dn = "/";

  char *filename = rstrscat (NULL, ".", dn, rpmfiBN (fi), NULL);
  archive_entry_copy_pathname (entry, filename);
  free (filename);

  archive_entry_set_size (entry, rpmfiFSize (fi));
  rpm_mode_t mode = rpmfiFMode (fi);
  archive_entry_set_filetype (entry, mode & S_IFMT);
  archive_entry_set_perm (entry, mode & ~S_IFMT);

  archive_entry_set_uname (entry, rpmfiFUser (fi));
  archive_entry_set_gname (entry, rpmfiFGroup (fi));
  archive_entry_set_rdev (entry, rpmfiFRdev (fi));
  archive_entry_set_mtime (entry, rpmfiFMtime (fi), 0);

  if (S_ISLNK (mode))
    archive_entry_set_symlink (entry, rpmfiFLink (fi));
}

static void
write_file_content (struct archive *a, char *buf, rpmfi fi)
{
  rpm_loff_t left = rpmfiFSize (fi);
  size_t len;
  ssize_t read_bytes;

  while (left > 0)
    {
      len = (left > BUFSIZE ? BUFSIZE : (size_t)left);
      read_bytes = rpmfiArchiveRead (fi, buf, len);
      if (read_bytes == (ssize_t)len)
        {
          archive_write_data (a, buf, len);
        }
      else
        {
          g_warning ("Error reading file payload from RPM");
          break;
        }
      left -= len;
    }
}

static void
skip_file_content (char *buf, rpmfi fi)
{
  rpm_loff_t left = rpmfiFSize (fi);
  size_t len;
  ssize_t read_bytes;

  while (left > 0)
    {
      len = (left > BUFSIZE ? BUFSIZE : (size_t)left);
      read_bytes = rpmfiArchiveRead (fi, buf, len);
      if (read_bytes != (ssize_t)len)
        break;
      left -= len;
    }
}

static char *
normalize_rpm_path (const char *path)
{
  if (!path)
    return NULL;
  if (path[0] == '/')
    return g_strdup_printf (".%s", path);
  if (path[0] == '.' && path[1] == '/')
    return g_strdup (path);
  return g_strdup_printf ("./%s", path);
}

int
run_rpm2archive (int argc, char **argv, GError **error)
{
  g_autofree gchar *opt_output = NULL;
  g_autofree gchar *opt_compression = NULL;
  gboolean opt_nocompression = FALSE;
  gboolean opt_verbose = FALSE;
  g_auto(GStrv) opt_files = NULL;

  const GOptionEntry entries[] = {
    { "output", 'o', 0, G_OPTION_ARG_STRING, &opt_output, "Output archive file path", "FILE" },
    { "nocompression", 'n', 0, G_OPTION_ARG_NONE, &opt_nocompression, "Create uncompressed tar archive", NULL },
    { "compression", 'C', 0, G_OPTION_ARG_STRING, &opt_compression, "Compression format: gzip, zstd, none, or auto", "FORMAT" },
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Verbose logging", NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_files, "Input RPM files", "RPM..." },
    { NULL }
  };

  g_autoptr(GOptionContext) opt_ctx = g_option_context_new ("[OPTIONS...] <RPM...>");
  g_option_context_add_main_entries (opt_ctx, entries, NULL);
  if (!g_option_context_parse (opt_ctx, &argc, &argv, error))
    return 1;

  /* Zero input RPMs is valid: the result is an empty archive. */
  guint num_pkgs = opt_files ? g_strv_length (opt_files) : 0;

  /* Hermetic host portability: if /usr/lib/rpm/rpmrc is absent and RPM_CONFIGDIR is unset */
  if (access ("/usr/lib/rpm/rpmrc", R_OK) != 0 && !getenv ("RPM_CONFIGDIR"))
    {
      const char *tmp = g_get_tmp_dir ();
      g_autofree char *dir = g_build_filename (tmp, "microdnf_rpm_cfg", NULL);
      g_mkdir_with_parents (dir, 0755);
      g_autofree char *rcfile = g_build_filename (dir, "rpmrc", NULL);
      if (access (rcfile, F_OK) != 0)
        g_file_set_contents (rcfile, "", 0, NULL);
      g_autofree char *macfile = g_build_filename (dir, "macros", NULL);
      if (access (macfile, F_OK) != 0)
        g_file_set_contents (macfile, "", 0, NULL);
      setenv ("RPM_CONFIGDIR", dir, 1);
    }
  rpmReadConfigFiles (NULL, NULL);

  rpmts ts = rpmtsCreate ();
  rpmVSFlags vsflags = RPMVSF_MASK_NODIGESTS | RPMVSF_MASK_NOSIGNATURES | RPMVSF_NOHDRCHK;
  rpmtsSetVSFlags (ts, vsflags);

  /* Pass 1: Scan headers of all RPMs in user-supplied order (0 .. num_pkgs - 1).
   * For files and symlinks: later RPMs win conflicts.
   * For directories: record directory presence. */
  g_autoptr(GHashTable) winner_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_autoptr(GHashTable) all_dirs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (guint i = 0; i < num_pkgs; ++i)
    {
      const char *fn = opt_files[i];
      FD_t fdi = Fopen (fn, "r.ufdio");
      if (Ferror (fdi))
        {
          g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                       "Failed to open RPM '%s': %s", fn, Fstrerror (fdi));
          rpmtsFree (ts);
          return 1;
        }

      Header h;
      int rc = rpmReadPackageFile (ts, fdi, "rpm2archive", &h);
      if (rc != RPMRC_OK && rc != RPMRC_NOKEY && rc != RPMRC_NOTTRUSTED)
        {
          g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                       "Failed to read header from '%s'", fn);
          Fclose (fdi);
          rpmtsFree (ts);
          return 1;
        }

      rpmfiles files = rpmfilesNew (NULL, h, 0, RPMFI_KEEPHEADER);
      int fc = rpmfilesFC (files);
      for (int ix = 0; ix < fc; ++ix)
        {
          char *raw_path = rpmfilesFN (files, ix);
          char *norm_path = normalize_rpm_path (raw_path);
          free (raw_path);

          rpm_mode_t mode = rpmfilesFMode (files, ix);
          if (S_ISDIR (mode))
            {
              g_hash_table_add (all_dirs, norm_path);
            }
          else
            {
              /* Later package in the pre-ordered list wins the path conflict */
              g_hash_table_insert (winner_map, norm_path, GUINT_TO_POINTER (i));
            }
        }

      rpmfilesFree (files);
      headerFree (h);
      Fclose (fdi);
    }

  /* Pass 2: Create target archive and write entries in package order */
  struct archive *a = archive_write_new ();

  gboolean use_zstd = FALSE;
  gboolean use_gzip = FALSE;

  if (opt_nocompression || (opt_compression && g_ascii_strcasecmp (opt_compression, "none") == 0))
    {
      /* Uncompressed tar */
    }
  else if (opt_compression && (g_ascii_strcasecmp (opt_compression, "zstd") == 0 ||
                               g_ascii_strcasecmp (opt_compression, "zst") == 0))
    {
      use_zstd = TRUE;
    }
  else if (opt_compression && (g_ascii_strcasecmp (opt_compression, "gzip") == 0 ||
                               g_ascii_strcasecmp (opt_compression, "gz") == 0))
    {
      use_gzip = TRUE;
    }
  else if (opt_output)
    {
      if (g_str_has_suffix (opt_output, ".tar.zst") || g_str_has_suffix (opt_output, ".tzst"))
        use_zstd = TRUE;
      else if (g_str_has_suffix (opt_output, ".tar.gz") || g_str_has_suffix (opt_output, ".tgz"))
        use_gzip = TRUE;
      else if (g_str_has_suffix (opt_output, ".tar"))
        {
          /* uncompressed tar */
        }
      else
        use_gzip = TRUE;
    }
  else
    {
      /* Default to gzip for stdout streaming */
      use_gzip = TRUE;
    }

  if ((use_zstd && archive_write_add_filter_zstd (a) != ARCHIVE_OK) ||
      (use_gzip && archive_write_add_filter_gzip (a) != ARCHIVE_OK))
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to enable %s compression: %s",
                   use_zstd ? "zstd" : "gzip", archive_error_string (a));
      archive_write_free (a);
      rpmtsFree (ts);
      return 1;
    }

  if (archive_write_set_format_pax_restricted (a) != ARCHIVE_OK)
    {
      g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                           "Failed to set pax restricted archive format");
      archive_write_free (a);
      rpmtsFree (ts);
      return 1;
    }

  if (opt_output && strcmp (opt_output, "-") != 0)
    {
      if (archive_write_open_filename (a, opt_output) != ARCHIVE_OK)
        {
          g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                       "Failed to open output archive file '%s': %s",
                       opt_output, archive_error_string (a));
          archive_write_free (a);
          rpmtsFree (ts);
          return 1;
        }
    }
  else
    {
      if (isatty (STDOUT_FILENO))
        {
          g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                               "Refusing to write archive binary stream to a terminal; specify -o <file>");
          archive_write_free (a);
          rpmtsFree (ts);
          return 1;
        }
      if (archive_write_open_fd (a, STDOUT_FILENO) != ARCHIVE_OK)
        {
          g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                       "Failed to open stdout for writing archive: %s",
                       archive_error_string (a));
          archive_write_free (a);
          rpmtsFree (ts);
          return 1;
        }
    }

  g_autoptr(GHashTable) emitted_dirs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  struct archive_entry *entry = archive_entry_new ();
  char *buf = g_malloc (BUFSIZE);

    for (guint i = 0; i < num_pkgs; ++i)
    {
      const char *fn = opt_files[i];
      FD_t fdi = Fopen (fn, "r.ufdio");
      if (Ferror (fdi))
        {
          if (opt_verbose)
            g_printerr ("Failed to open '%s': %s\n", fn, Fstrerror (fdi));
          continue;
        }

      Header h;
      int rc = rpmReadPackageFile (ts, fdi, "rpm2archive", &h);
      if (rc != RPMRC_OK && rc != RPMRC_NOKEY && rc != RPMRC_NOTTRUSTED)
        {
          if (opt_verbose)
            g_printerr ("Failed to read package file '%s': rc=%d\n", fn, rc);
          Fclose (fdi);
          continue;
        }

      const char *compr = headerGetString (h, RPMTAG_PAYLOADCOMPRESSOR);
      char *rpmio_flags = rstrscat (NULL, "r.", compr ? compr : "gzip", NULL);
      FD_t gzdi = Fdopen (fdi, rpmio_flags);
      free (rpmio_flags);

      if (!gzdi)
        {
          if (opt_verbose)
            g_printerr ("Failed to re-open payload for '%s'\n", fn);
          headerFree (h);
          Fclose (fdi);
          continue;
        }

      rpmfiles files = rpmfilesNew (NULL, h, 0, RPMFI_KEEPHEADER);
      rpmfi fi = rpmfiNewArchiveReader (gzdi, files, RPMFI_ITER_READ_ARCHIVE_CONTENT_FIRST);

      char *hardlink = NULL;

      if (opt_verbose)
        g_print ("Processing package [%u/%u]: %s\n", i + 1, num_pkgs, fn);

      while ((rc = rpmfiNext (fi)) >= 0)
        {
          fill_archive_entry (entry, fi);
          const char *entry_path = archive_entry_pathname (entry);
          rpm_mode_t mode = rpmfiFMode (fi);
          int nlink = rpmfiFNlink (fi);
          gboolean has_content = S_ISREG (mode) && (nlink == 1 || rpmfiArchiveHasContent (fi));

          if (S_ISDIR (mode))
            {
              /* If a non-directory in any package replaces this path, skip directory */
              if (g_hash_table_contains (winner_map, entry_path))
                continue;

              /* Emit directory once across all RPMs */
              if (g_hash_table_contains (emitted_dirs, entry_path))
                continue;

              if (opt_verbose)
                g_print ("  [DIR] %s\n", entry_path);

              g_hash_table_add (emitted_dirs, g_strdup (entry_path));
              archive_write_header (a, entry);
            }
          else
            {
              gpointer winner_pkg_ptr = g_hash_table_lookup (winner_map, entry_path);
              guint winner_pkg = GPOINTER_TO_UINT (winner_pkg_ptr);

              if (winner_pkg != i)
                {
                  /* Superseded by a later RPM in the pre-ordered list; discard payload */
                  if (has_content)
                    skip_file_content (buf, fi);
                  continue;
                }

              if (opt_verbose)
                g_print ("  [FILE] %s\n", entry_path);

              if (nlink > 1)
                {
                  if (rpmfiArchiveHasContent (fi))
                    {
                      g_free (hardlink);
                      hardlink = g_strdup (entry_path);
                    }
                  else if (hardlink)
                    {
                      archive_entry_set_hardlink (entry, hardlink);
                    }
                }

              archive_write_header (a, entry);
              if (has_content)
                write_file_content (a, buf, fi);
            }
        }

      g_free (hardlink);
      rpmfiArchiveClose (fi);
      rpmfilesFree (files);
      headerFree (h);
      Fclose (gzdi);
    }

  g_free (buf);
  archive_entry_free (entry);
  archive_write_close (a);
  archive_write_free (a);
  rpmtsFree (ts);

  return 0;
}
