#ifndef RPM_CONFIG_H
#define RPM_CONFIG_H
#define _GNU_SOURCE 1
#define PACKAGE "rpm"
#define PACKAGE_BUGREPORT "https://rpm.org"
#define PACKAGE_NAME "rpm"
#define PACKAGE_STRING "rpm 4.18.2"
#define PACKAGE_TARNAME "rpm"
#define PACKAGE_URL "http://www.rpm.org"
#define PACKAGE_VERSION "4.18.2"
#define VERSION "4.18.2"
#define RPMCANONVENDOR "redhat"
#define RUNDIR "/run"
#define GID_0_GROUP "root"
#define UID_0_USER "root"
#define __MKDIR_P "/bin/mkdir -p"
#define ENABLE_NDB 1
#define ENABLE_PLUGINS 1
#define WITH_BDB_RO 1
#define WITH_SQLITE 1
#define HAVE_SQLITE 1
#define HAVE_ZLIB_H 1
#define HAVE_BZLIB_H 1
#define HAVE_LZMA_H 1
#define HAVE_ZSTD 1
#define HAVE_LIBCRYPTO 1
#define HAVE_OPENSSL 1
#define HAVE_BN2BINPAD 1
#define HAVE_DSA_SET0_KEY 1
#define HAVE_DSA_SET0_PQG 1
#define HAVE_DSA_SIG_SET0 1
#define HAVE_EVP_MD_CTX_NEW 1
#define HAVE_RSA_SET0_KEY 1
#define HAVE_BASENAME 1
#if defined(__linux__)
#define HAVE_DECL_FDATASYNC 1
#endif
#define HAVE_DIRENT_H 1
#define HAVE_DIRNAME 1
#define HAVE_DLFCN_H 1
#define HAVE_FCHMODAT 1
#define HAVE_FCHOWNAT 1
#if defined(__linux__)
#define HAVE_FDATASYNC 1
#endif
#define HAVE_FSTATAT 1
#define HAVE_GETADDRINFO 1
#if defined(__linux__)
#define HAVE_GETAUXVAL 1
#endif
#define HAVE_GETCWD 1
#define HAVE_GETLINE 1
#define HAVE_GETOPT_H 1
#define HAVE_GZSEEK 1
#define HAVE_ICONV 1
#define ICONV_CONST
#define HAVE_INTTYPES_H 1
#define HAVE_LCHOWN 1
#define HAVE_LIBPTHREAD 1
#define HAVE_LIMITS_H 1
#define HAVE_LINKAT 1
#define HAVE_LOCALTIME_R 1
#define HAVE_LSETXATTR 1
#define HAVE_MADVISE 1
#if defined(__linux__)
#define HAVE_MEMPCPY 1
#endif
#define HAVE_MKDTEMP 1
#define HAVE_MKFIFO 1
#define HAVE_MKFIFOAT 1
#define HAVE_MKNODAT 1
#define HAVE_MKSTEMP 1
#define HAVE_OPENAT 1
#define HAVE_POLL_H 1
#if defined(__linux__)
#define HAVE_POSIX_FADVISE 1
#endif
#if defined(__linux__)
#define HAVE_POSIX_FALLOCATE 1
#endif
#define HAVE_PTHREAD_H 1
#define HAVE_PUTENV 1
#define HAVE_RAND_BYTES 1
#define HAVE_READLINKAT 1
#define HAVE_REALPATH 1
#define HAVE_RENAMEAT 1
#if defined(__linux__)
#define HAVE_SCHED_GETAFFINITY 1
#endif
#if defined(__linux__)
#define HAVE_SECURE_GETENV 1
#endif
#define HAVE_SETENV 1
#define HAVE_STATVFS 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STPCPY 1
#define HAVE_STPNCPY 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRUCT_DIRENT_D_TYPE 1
#define HAVE_SYMLINKAT 1
#if defined(__linux__)
#define HAVE_SYNCFS 1
#endif
#if defined(__linux__)
#define HAVE_SYS_AUXV_H 1
#endif
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_UNLINKAT 1
#define HAVE_UNSETENV 1
#if defined(__linux__)
#define HAVE_UNSHARE 1
#endif
#define HAVE_UTIMENSAT 1
#define HAVE_UTIMES 1
#define HAVE_WCHAR_H 1
#if defined(__APPLE__)
#define HAVE_SETPROGNAME 1
#else
#define HAVE___PROGNAME 1
#endif
#if defined(__linux__)
#define MAJOR_IN_SYSMACROS 1
#endif
#define STDC_HEADERS 1
#define RPMCONFIGDIR "/usr/lib/rpm"
#define LOCALSTATEDIR "/var"
#define LOCALEDIR "/usr/share/locale"
#define SYSCONFDIR "/etc"
#define LIBRPMALIAS_FILENAME "rpmpopt-4.18.2"
#define LIBRPMALIAS_EXECPATH "/usr/bin"
#endif
