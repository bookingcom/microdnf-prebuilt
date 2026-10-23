#ifndef GPG_ERROR_CONFIG_H
#define GPG_ERROR_CONFIG_H
#define _GNU_SOURCE 1
#define PACKAGE "libgpg-error"
#define PACKAGE_NAME "libgpg-error"
#define PACKAGE_VERSION "1.51"
#define VERSION "1.51"
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_LOCALE_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_DLFCN_H 1
#define HAVE_PWD_H 1
#define HAVE_POLL_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_LANGINFO_CODESET 1
#define HAVE_VASPRINTF 1
#define HAVE_STPCPY 1
#define HAVE_STRERROR_R 1
#if defined(__linux__)
#define STRERROR_R_CHAR_P 1
#endif
#define HAVE_FLOCKFILE 1
#define HAVE_FORK 1
#define HAVE_GETADDRINFO 1
#define HAVE_GETRLIMIT 1
#define HAVE_MMAP 1
#define HAVE_PIPE 1
#define HAVE_POLL 1
#define HAVE_RAND 1
#define HAVE_READLINK 1
#define HAVE_SETLOCALE 1
#define HAVE_STAT 1
#define HAVE_STRTOLL 1
#define HAVE_TIMEGM 1
#define HAVE_WAIT4 1
#define HAVE_WAITPID 1
#define HAVE_INTMAX_T 1
#define HAVE_UINTMAX_T 1
#define HAVE_LONG_DOUBLE 1
#define HAVE_LONG_LONG_INT 1
#define HAVE_UNSIGNED_LONG_LONG_INT 1
#define HAVE_PTRDIFF_T 1
#define HAVE_WEAK_SYMBOLS 1
#define HAVE_GCC_ATTRIBUTE_ALIGNED 1
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF_UNSIGNED_LONG 8
#define SIZEOF_VOID_P 8
#define SIZEOF_TIME_T 8
#ifdef __aarch64__
#define SIZEOF_PTHREAD_MUTEX_T 48
#define HOST_TRIPLET_STRING "aarch64-unknown-linux-gnu"
#else
#define SIZEOF_PTHREAD_MUTEX_T 40
#define HOST_TRIPLET_STRING "x86_64-unknown-linux-gnu"
#if defined(__linux__)
#define HAVE_MEMRCHR 1
#endif
#define HAVE_SETENV 1
#define BUILD_COMMITID "0"
#define BUILD_REVISION "0"
#define BUILD_TIMESTAMP "2025-01-01T00:00+0000"
#endif
#define REPLACEMENT_FOR_OFF_T "off_t"
#define USE_POSIX_THREADS 1
#define _ESTREAM_PRINTF_REALLOC _gpgrt_realloc
#define _ESTREAM_PRINTF_EXTRA_INCLUDE "gpgrt-int.h"
#define GPG_ERR_ENABLE_GETTEXT_MACROS 1
#define GPG_ERR_ENABLE_ERRNO_MACROS 1
#define GPGRT_ENABLE_ES_MACROS 1
#define GPGRT_ENABLE_LOG_MACROS 1
#define GPGRT_ENABLE_ARGPARSE_MACROS 1
#if defined(__linux__)
#define HAVE_MEMRCHR 1
#endif
#define HAVE_SETENV 1
#define BUILD_COMMITID "0"
#define BUILD_REVISION "0"
#define BUILD_TIMESTAMP "2025-01-01T00:00+0000"
#endif
