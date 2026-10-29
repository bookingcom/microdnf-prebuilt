// Force-included into every dnf5 C++ translation unit (see BUILD.bazel). It stands in for patches that would
// otherwise touch many upstream files: dnf5 is written against glibc and libstdc++.
#pragma once

// libstdc++ pulls these in transitively, libc++ does not.
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <vector>

#ifdef __APPLE__
#include <cerrno>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

// Linux-only calls dnf5 makes directly.
static inline int pipe2(int fds[2], int flags) {
    if (pipe(fds) == -1) {
        return -1;
    }
    if (flags & O_CLOEXEC) {
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }
    return 0;
}

#define RENAME_EXCHANGE RENAME_SWAP
static inline int renameat2(int, const char * from, int, const char * to, unsigned flags) {
    return renamex_np(from, to, flags);
}

// Mount namespaces do not exist on macOS; bootc is not either, so this is only reached on a bootc host.
#define CLONE_NEWNS 0x00020000
static inline int unshare(int) {
    errno = ENOSYS;
    return -1;
}
#endif
