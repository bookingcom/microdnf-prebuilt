#pragma once

#include <glib.h>

#ifndef XMLB_MAJOR_VERSION
#define XMLB_MAJOR_VERSION (0)
#endif

#ifndef XMLB_MINOR_VERSION
#define XMLB_MINOR_VERSION (3)
#endif

#ifndef XMLB_MICRO_VERSION
#define XMLB_MICRO_VERSION (29)
#endif

#define LIBXMLB_CHECK_VERSION(major,minor,micro) \
    (XMLB_MAJOR_VERSION > (major) || \
     (XMLB_MAJOR_VERSION == (major) && XMLB_MINOR_VERSION > (minor)) || \
     (XMLB_MAJOR_VERSION == (major) && XMLB_MINOR_VERSION == (minor) && \
      XMLB_MICRO_VERSION >= (micro)))

G_BEGIN_DECLS

const gchar *xb_version_string (void);

G_END_DECLS
