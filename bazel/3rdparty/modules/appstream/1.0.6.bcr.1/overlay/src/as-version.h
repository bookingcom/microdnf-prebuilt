#pragma once

#if !defined (__APPSTREAM_H_INSIDE__) && !defined (AS_COMPILATION)
#error "Only <appstream.h> can be included directly."
#endif

#include <glib.h>

G_BEGIN_DECLS

#define AS_MAJOR_VERSION 1
#define AS_MINOR_VERSION 0
#define AS_MICRO_VERSION 6

#define AS_CHECK_VERSION(major,minor,micro) \
    (AS_MAJOR_VERSION > (major) || \
     (AS_MAJOR_VERSION == (major) && AS_MINOR_VERSION > (minor)) || \
     (AS_MAJOR_VERSION == (major) && AS_MINOR_VERSION == (minor) && \
      AS_MICRO_VERSION >= (micro)))

const gchar *as_version_string (void);

G_END_DECLS
