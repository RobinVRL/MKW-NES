// Run on the NES/SNES Classic's glibc 2.23 (kernel 3.4) although the
// toolchain's glibc is newer. Every reference to a symbol version newer than
// the ARM baseline (GLIBC_2.4) is redirected here with -Wl,--wrap=<name>
// (port/build.sh) - including references inside the static libstdc++.
// time64 entry points are implemented on the 32-bit syscalls.
#define _FILE_OFFSET_BITS 64
#define _TIME_BITS 64
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#define OLD(ret, name, params) \
    ret __old_##name params;  \
    __asm__(".symver __old_" #name "," #name "@GLIBC_2.4");

struct ts64 { int64_t sec; int32_t nsec; int32_t pad; };
struct ts32 { int32_t sec; int32_t nsec; };
static void to32(const struct ts64* a, struct ts32* b) { b->sec = (int32_t)a->sec; b->nsec = a->nsec; }
static void to64(const struct ts32* a, struct ts64* b) { b->sec = a->sec; b->nsec = a->nsec; b->pad = 0; }

// Data symbol from libstdc++ (glibc 2.32); 0 = assume multithreaded.
char __libc_single_threaded = 0;

// ---------------------------------------------------------------- startup / threads / dl
OLD(int, __libc_start_main, (int (*)(int, char**, char**), int, char**, void (*)(void), void (*)(void), void (*)(void), void*))
// glibc >= 2.34's crt1.o passes init = fini = NULL and leaves running the
// program's constructors to libc. glibc 2.22 runs only what it is given, so
// supply the old __libc_csu_init/fini behaviour (the arrays come from the linker).
extern void (*__preinit_array_start[])(int, char**, char**) __attribute__((visibility("hidden")));
extern void (*__preinit_array_end[])(int, char**, char**) __attribute__((visibility("hidden")));
extern void (*__init_array_start[])(int, char**, char**) __attribute__((visibility("hidden")));
extern void (*__init_array_end[])(int, char**, char**) __attribute__((visibility("hidden")));
extern void (*__fini_array_start[])(void) __attribute__((visibility("hidden")));
extern void (*__fini_array_end[])(void) __attribute__((visibility("hidden")));
static void csu_init(int argc, char** argv, char** envp) {
    for (size_t k = 0; k < (size_t)(__preinit_array_end - __preinit_array_start); ++k) __preinit_array_start[k](argc, argv, envp);
    for (size_t k = 0; k < (size_t)(__init_array_end - __init_array_start); ++k) __init_array_start[k](argc, argv, envp);
}
static void csu_fini(void) {
    for (size_t k = (size_t)(__fini_array_end - __fini_array_start); k-- > 0;) __fini_array_start[k]();
}
int __wrap___libc_start_main(int (*m)(int, char**, char**), int c, char** v, void (*i)(void), void (*f)(void), void (*r)(void), void* s) {
    if (!i) i = (void (*)(void))csu_init; // 2.22 calls init(argc, argv, envp)
    if (!f) f = csu_fini;
    return __old___libc_start_main(m, c, v, i, f, r, s);
}
#define PASS(ret, name, params, args) OLD(ret, name, params) ret __wrap_##name params { return __old_##name args; }
PASS(int, pthread_create, (void* t, const void* a, void* (*fn)(void*), void* arg), (t, a, fn, arg))
PASS(int, pthread_detach, (unsigned long t), (t))
PASS(int, pthread_join, (unsigned long t, void** r), (t, r))
PASS(int, pthread_once, (int* o, void (*fn)(void)), (o, fn))
PASS(int, pthread_attr_setstacksize, (void* a, size_t s), (a, s))
PASS(void*, dlopen, (const char* f, int m), (f, m))
PASS(void*, dlsym, (void* h, const char* s), (h, s))
PASS(char*, dlerror, (void), ())
PASS(int, dlclose, (void* h), (h))

// ---------------------------------------------------------------- time64
int __wrap___clock_gettime64(int clk, struct ts64* tp) {
    struct ts32 t;
    long r = syscall(__NR_clock_gettime, clk, &t);
    if (r == 0 && tp) to64(&t, tp);
    return (int)r;
}
int __wrap___clock_getres64(int clk, struct ts64* tp) {
    struct ts32 t;
    long r = syscall(__NR_clock_getres, clk, &t);
    if (r == 0 && tp) to64(&t, tp);
    return (int)r;
}
int __wrap___nanosleep64(const struct ts64* req, struct ts64* rem) {
    struct ts32 a, b = {0, 0};
    to32(req, &a);
    long r = syscall(__NR_nanosleep, &a, &b);
    if (rem) to64(&b, rem);
    return (int)r;
}
int __wrap___clock_nanosleep_time64(int clk, int flags, const struct ts64* req, struct ts64* rem) {
    struct ts32 a, b = {0, 0};
    to32(req, &a);
    long r = syscall(__NR_clock_nanosleep, clk, flags, &a, &b);
    if (rem) to64(&b, rem);
    return r < 0 ? errno : 0; // clock_nanosleep returns the error
}
int __wrap___gettimeofday64(struct ts64* tv, void* tz) {
    struct ts32 t; // timeval32 has the same shape
    long r = syscall(__NR_gettimeofday, &t, tz);
    if (r == 0 && tv) { tv->sec = t.sec; tv->nsec = t.nsec; tv->pad = 0; }
    return (int)r;
}
int64_t __wrap___time64(int64_t* out) {
    struct ts64 t;
    __wrap___clock_gettime64(0, &t);
    if (out) *out = t.sec;
    return t.sec;
}
// ARM kernel struct stat64 (asm/stat.h)
struct kstat64 {
    unsigned long long dev; unsigned char pad0[4]; unsigned long ino32;
    unsigned int mode, nlink; unsigned long uid, gid;
    unsigned long long rdev; unsigned char pad3[4];
    long long size; unsigned long blksize; unsigned long long blocks;
    unsigned long at, atn, mt, mtn, ct, ctn;
    unsigned long long ino;
};
static void fromKstat(const struct kstat64* kp, struct stat* st);
int __wrap___fstat64_time64(int fd, struct stat* st) { // struct stat is the time64 one here
    struct kstat64 k;
    long r = syscall(__NR_fstat64, fd, &k);
    if (r) return (int)r;
    fromKstat(&k, st);
    return 0;
}
// stat() for port/ctru/src/os.cpp's path-translating stat: glibc 2.22 exports
// no plain stat (only __xstat64), and its struct is not the time64 one.
int portRawStat(const char* path, struct stat* st) {
    struct kstat64 k;
    long r = syscall(__NR_stat64, path, &k);
    if (r) return (int)r;
    fromKstat(&k, st);
    return 0;
}
static void fromKstat(const struct kstat64* kp, struct stat* st) {
    const struct kstat64 k = *kp;
    memset(st, 0, sizeof *st);
    st->st_dev = k.dev; st->st_ino = k.ino; st->st_mode = k.mode; st->st_nlink = k.nlink;
    st->st_uid = k.uid; st->st_gid = k.gid; st->st_rdev = k.rdev; st->st_size = k.size;
    st->st_blksize = (long)k.blksize; st->st_blocks = (long long)k.blocks;
    st->st_atim.tv_sec = k.at; st->st_atim.tv_nsec = (long)k.atn;
    st->st_mtim.tv_sec = k.mt; st->st_mtim.tv_nsec = (long)k.mtn;
    st->st_ctim.tv_sec = k.ct; st->st_ctim.tv_nsec = (long)k.ctn;
}
OLD(int, ioctl, (int, unsigned long, ...))
int __wrap___ioctl_time64(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    return __old_ioctl(fd, req, arg);
}
OLD(int, pthread_cond_timedwait, (void*, void*, const struct ts32*))
int __wrap___pthread_cond_timedwait64(void* c, void* m, const struct ts64* abs) {
    struct ts32 a;
    to32(abs, &a);
    return __old_pthread_cond_timedwait(c, m, &a);
}
// condition variables keep the default (realtime) clock: convert the deadline
int __wrap___pthread_cond_clockwait64(void* c, void* m, int clk, const struct ts64* abs) {
    struct ts64 now, rt;
    __wrap___clock_gettime64(clk, &now);
    __wrap___clock_gettime64(0, &rt);
    int64_t ns = (abs->sec - now.sec) * 1000000000LL + (abs->nsec - now.nsec);
    if (ns < 0) ns = 0;
    ns += rt.sec * 1000000000LL + rt.nsec;
    struct ts32 a = {(int32_t)(ns / 1000000000LL), (int32_t)(ns % 1000000000LL)};
    return __old_pthread_cond_timedwait(c, m, &a);
}
int __wrap___pthread_timedjoin_np64(unsigned long t, void** r, const struct ts64* abs) {
    (void)abs;
    return __old_pthread_join(t, r);
}
OLD(int, pthread_mutex_timedlock, (void*, const struct ts32*))
int __wrap___pthread_mutex_timedlock64(void* m, const struct ts64* abs) {
    struct ts32 a;
    to32(abs, &a);
    return __old_pthread_mutex_timedlock(m, &a);
}
OLD(int, sem_timedwait, (void*, const struct ts32*))
int __wrap___sem_timedwait64(void* s, const struct ts64* abs) {
    struct ts32 a;
    to32(abs, &a);
    return __old_sem_timedwait(s, &a);
}

// ---------------------------------------------------------------- C23 stdio/stdlib
OLD(int, vsscanf, (const char*, const char*, va_list))
OLD(int, vfscanf, (FILE*, const char*, va_list))
int __wrap___isoc23_sscanf(const char* s, const char* f, ...) {
    va_list ap; va_start(ap, f); int r = __old_vsscanf(s, f, ap); va_end(ap); return r;
}
int __wrap___isoc23_fscanf(FILE* s, const char* f, ...) {
    va_list ap; va_start(ap, f); int r = __old_vfscanf(s, f, ap); va_end(ap); return r;
}
int __wrap___isoc23_vsscanf(const char* s, const char* f, va_list ap) { return __old_vsscanf(s, f, ap); }
int __wrap___isoc23_vfscanf(FILE* s, const char* f, va_list ap) { return __old_vfscanf(s, f, ap); }
PASS(long, strtol, (const char* s, char** e, int b), (s, e, b))
PASS(unsigned long, strtoul, (const char* s, char** e, int b), (s, e, b))
PASS(long long, strtoll, (const char* s, char** e, int b), (s, e, b))
PASS(unsigned long long, strtoull, (const char* s, char** e, int b), (s, e, b))
long __wrap___isoc23_strtol(const char* s, char** e, int b) { return __old_strtol(s, e, b); }
unsigned long __wrap___isoc23_strtoul(const char* s, char** e, int b) { return __old_strtoul(s, e, b); }
long long __wrap___isoc23_strtoll(const char* s, char** e, int b) { return __old_strtoll(s, e, b); }
unsigned long long __wrap___isoc23_strtoull(const char* s, char** e, int b) { return __old_strtoull(s, e, b); }

// ---------------------------------------------------------------- libm (newer default versions)
PASS(float, expf, (float x), (x))
PASS(float, exp2f, (float x), (x))
PASS(float, logf, (float x), (x))
PASS(float, log2f, (float x), (x))
PASS(float, powf, (float x, float y), (x, y))
PASS(double, pow, (double x, double y), (x, y))
PASS(double, exp, (double x), (x))
PASS(double, log, (double x), (x))
PASS(double, exp2, (double x), (x))
PASS(double, log2, (double x), (x))
PASS(float, hypotf, (float x, float y), (x, y))
PASS(double, fmod, (double x, double y), (x, y))
PASS(float, fmodf, (float x, float y), (x, y))
PASS(float, acosf, (float x), (x))
PASS(float, asinf, (float x), (x))
PASS(float, atan2f, (float y, float x), (y, x))
PASS(float, remainderf, (float x, float y), (x, y))
PASS(float, sqrtf, (float x), (x))
PASS(float, sinf, (float x), (x))
PASS(float, cosf, (float x), (x))
