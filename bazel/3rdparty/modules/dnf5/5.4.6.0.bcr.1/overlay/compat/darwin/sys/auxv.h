// macOS has no <sys/auxv.h>. dnf5 only asks for the ARM hardware capabilities, which do not apply here.
#pragma once

#define AT_HWCAP 16

static inline unsigned long getauxval(unsigned long) {
    return 0;
}
