// glibc_compat.h -- the Linux release build (tools/release/package_linux.sh, make LINUX_RELEASE=1) force-includes this
// header (-include) into every C / C++ source, so programs built on a new distro (Ubuntu 26.04: glibc 2.43) run on
// glibc 2.35 (Ubuntu 22.04, the oldest distro the tester package supports; SteamOS / Fedora are newer):
//   - no C23 strtol / scanf redirection (glibc 2.38's __isoc23_strtol ...; _GNU_SOURCE turns it on): the classic
//     functions instead (the only difference: C23 reads a "0b" prefix with base 0 / 2; the port never relies on it);
//   - the math functions glibc 2.38 - 2.43 gave new default versions (the SVID error wrappers removed; same results)
//     bound to their old versions;
//   - (C++) __isoc23_strtoul and arc4random (2.36) for the static libstdc++ (-static-libstdc++: GCC 15's libstdc++.a
//     was compiled against 2.43 headers): the classic strtoul, the kernel's getrandom.
// package_linux.sh checks the result: no GLIBC_ version above 2.35 in any packaged program.
// No effect off Linux x86-64 / glibc.
#ifndef B3COMPAT_GLIBC_COMPAT_H
#define B3COMPAT_GLIBC_COMPAT_H
#if defined(__linux__) && defined(__x86_64__)
#include <features.h>
#if defined(__GLIBC__)
#undef __GLIBC_USE_C23_STRTOL
#define __GLIBC_USE_C23_STRTOL 0
// name, old version (objdump -T libm.so.6 / libc.so.6 on glibc 2.43: every symbol whose default version is newer than
// 2.35 and that has an older one)
#define B3COMPAT_OLD(name, ver) __asm__(".symver " #name "," #name "@" #ver);
B3COMPAT_OLD(acosf, GLIBC_2.2.5)
B3COMPAT_OLD(acoshf, GLIBC_2.2.5)
B3COMPAT_OLD(asinf, GLIBC_2.2.5)
B3COMPAT_OLD(atan2f, GLIBC_2.2.5)
B3COMPAT_OLD(atanhf, GLIBC_2.2.5)
B3COMPAT_OLD(coshf, GLIBC_2.2.5)
B3COMPAT_OLD(exp10, GLIBC_2.2.5)
B3COMPAT_OLD(fmod, GLIBC_2.2.5)
B3COMPAT_OLD(fmodf, GLIBC_2.2.5)
B3COMPAT_OLD(j0f, GLIBC_2.2.5)
B3COMPAT_OLD(j1f, GLIBC_2.2.5)
B3COMPAT_OLD(jnf, GLIBC_2.2.5)
B3COMPAT_OLD(lgammaf, GLIBC_2.23)
B3COMPAT_OLD(lgammaf_r, GLIBC_2.2.5)
B3COMPAT_OLD(log10f, GLIBC_2.2.5)
B3COMPAT_OLD(remainder, GLIBC_2.2.5)
B3COMPAT_OLD(remainderf, GLIBC_2.2.5)
B3COMPAT_OLD(sinhf, GLIBC_2.2.5)
B3COMPAT_OLD(sqrtf, GLIBC_2.2.5)
B3COMPAT_OLD(tgammaf, GLIBC_2.2.5)
B3COMPAT_OLD(y0f, GLIBC_2.2.5)
B3COMPAT_OLD(y1f, GLIBC_2.2.5)
B3COMPAT_OLD(ynf, GLIBC_2.2.5)
B3COMPAT_OLD(cfgetispeed, GLIBC_2.2.5)
B3COMPAT_OLD(cfgetospeed, GLIBC_2.2.5)
B3COMPAT_OLD(cfsetispeed, GLIBC_2.2.5)
B3COMPAT_OLD(cfsetospeed, GLIBC_2.2.5)
B3COMPAT_OLD(cfsetspeed, GLIBC_2.2.5)
#undef B3COMPAT_OLD
#if defined(__cplusplus)
// (declared as <stdlib.h> does, without including it here); one copy per program (an inline function's COMDAT),
// emitted in every unit so the link always finds it; hidden (a shared library does not export them)
extern "C" unsigned long strtoul(const char* __restrict s, char** __restrict end, int base) noexcept;
extern "C" inline __attribute__((used, visibility("hidden"))) unsigned long __isoc23_strtoul(const char* s, char** end,
                                                                                            int base) noexcept {
    return strtoul(s, end, base);
}
// arc4random (glibc 2.36; std::random_device in the static libstdc++): the kernel's getrandom (Linux 3.17), called
// directly (no declaration to clash with <sys/random.h>), retried when interrupted
extern "C" inline __attribute__((used, visibility("hidden"))) unsigned int arc4random(void) noexcept {
    unsigned int v = 0;
    for (int tries = 0; tries < 64; ++tries) {
        long r;
        __asm__ volatile("syscall" : "=a"(r) : "0"(318L /* SYS_getrandom */), "D"(&v), "S"(sizeof v), "d"(0)
                         : "rcx", "r11", "memory");
        if (r == static_cast<long>(sizeof v)) return v;
        if (r != -4 /* EINTR */) break;
    }
    unsigned int lo, hi;   // no getrandom: the time stamp counter (std::random_device only seeds the port's shuffles)
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return lo ^ (hi * 2654435761u) ^ v;
}
#endif
#endif  // __GLIBC__
#endif  // __linux__ && __x86_64__
#endif  // B3COMPAT_GLIBC_COMPAT_H
