// libctru replacement for Linux (NES/SNES Classic): time, threads, locks,
// memory, APT/CFGU/applets and path translation for sdmc:/ and romfs:/.
// Behaviour follows libctru's documented semantics; implementation is new.
#include <3ds.h>
#include <atomic>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <vector>
#include <sys/syscall.h>
#include <unistd.h>
#include "port_paths.hpp"

// ---------------------------------------------------------------- futex
static long futex(volatile s32* addr, int op, s32 val, const struct timespec* ts = nullptr) {
    return syscall(SYS_futex, (s32*)addr, op | FUTEX_PRIVATE_FLAG, val, ts, nullptr, 0);
}
static struct timespec nsToTs(s64 ns) {
    if (ns < 0) ns = 0;
    struct timespec ts;
    ts.tv_sec = time_t(ns / 1000000000LL);
    ts.tv_nsec = long(ns % 1000000000LL);
    return ts;
}

extern "C" {

// ---------------------------------------------------------------- heap globals
// Values the game reads for diagnostics/budgeting (animation.cpp heapSpaceFree,
// main.cpp envGetHeapSize). The NES Mini has 256 MiB; leave room for the OS.
static char heapAnchor[16];
u32 __ctru_heap_size = 96u << 20;
char* fake_heap_start = heapAnchor;
char* fake_heap_end = heapAnchor + (96u << 20);

// ---------------------------------------------------------------- time / svc
static u64 monoNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return u64(ts.tv_sec) * 1000000000ULL + u64(ts.tv_nsec);
}
u64 svcGetSystemTick(void) {
    // SYSCLOCK_ARM11 ticks (268111856 Hz), as the 3DS counter.
    const u64 ns = monoNs();
    return (ns / 1000000000ULL) * u64(SYSCLOCK_ARM11) + (ns % 1000000000ULL) * u64(SYSCLOCK_ARM11) / 1000000000ULL;
}
u64 osGetTime(void) {
    // Milliseconds since 1 January 1900, as libctru.
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (u64(ts.tv_sec) + 2208988800ULL) * 1000ULL + u64(ts.tv_nsec) / 1000000ULL;
}
void osSetSpeedupEnable(bool) {}
void svcSleepThread(s64 ns) {
    struct timespec ts = nsToTs(ns);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}
Result svcCloseHandle(Handle) { return 0; }
Result svcGetThreadPriority(s32* out, Handle) { if (out) *out = 0x30; return 0; }
Result svcSetThreadPriority(Handle, s32) { return 0; }
Result srvGetServiceHandle(Handle* out, const char*) { if (out) *out = 0; return (Result)0xD8E06406; }
Result svcSendSyncRequest(Handle) { return (Result)0xD8E06406; }

// ---------------------------------------------------------------- threads
struct Thread_tag {
    pthread_t pt;
    ThreadFunc fn;
    void* arg;
    bool detached;
    std::atomic<bool> finished{false};
};
static void* threadTrampoline(void* p) {
    Thread t = static_cast<Thread>(p);
    t->fn(t->arg);
    const bool detached = t->detached;
    t->finished = true;
    if (detached) delete t;
    return nullptr;
}
Thread threadCreate(ThreadFunc entry, void* arg, size_t stack_size, int, int, bool detached) {
    Thread t = new Thread_tag;
    t->fn = entry; t->arg = arg; t->detached = detached;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack_size < 256 * 1024) stack_size = 256 * 1024;
    pthread_attr_setstacksize(&attr, (stack_size + 4095) & ~size_t(4095));
    if (detached) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    const int rc = pthread_create(&t->pt, &attr, threadTrampoline, t);
    pthread_attr_destroy(&attr);
    if (rc) { delete t; return nullptr; }
    return t;
}
Result threadJoin(Thread t, u64 timeout_ns) {
    if (!t) return -1;
    if (timeout_ns == U64_MAX) { pthread_join(t->pt, nullptr); return 0; }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    u64 ns = u64(ts.tv_nsec) + timeout_ns % 1000000000ULL;
    ts.tv_sec += time_t(timeout_ns / 1000000000ULL + ns / 1000000000ULL);
    ts.tv_nsec = long(ns % 1000000000ULL);
    return pthread_timedjoin_np(t->pt, nullptr, &ts) == 0 ? 0 : (Result)0x09401BFE;
}
void threadFree(Thread t) { if (t && !t->detached) delete t; }
Handle threadGetHandle(Thread t) { return t ? Handle(uintptr_t(t)) : 0; }

// ---------------------------------------------------------------- locks
// LightLock: 0/1 unlocked (static zero or LightLock_Init), 2 locked, 3 locked+waiters.
void LightLock_Init(LightLock* lock) { __atomic_store_n(lock, 1, __ATOMIC_RELEASE); }
void LightLock_Lock(LightLock* lock) {
    for (;;) {
        s32 v = __atomic_load_n(lock, __ATOMIC_RELAXED);
        if (v <= 1) {
            if (__atomic_compare_exchange_n(lock, &v, 2, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
            continue;
        }
        if (v == 2 && !__atomic_compare_exchange_n(lock, &v, 3, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) continue;
        futex(lock, FUTEX_WAIT, 3);
        // Re-acquire as contended so the unlocker keeps waking.
        for (;;) {
            s32 w = __atomic_load_n(lock, __ATOMIC_RELAXED);
            if (w <= 1) {
                if (__atomic_compare_exchange_n(lock, &w, 3, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
                continue;
            }
            if (w == 2) __atomic_compare_exchange_n(lock, &w, 3, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
            futex(lock, FUTEX_WAIT, 3);
        }
    }
}
int LightLock_TryLock(LightLock* lock) {
    s32 v = __atomic_load_n(lock, __ATOMIC_RELAXED);
    return (v <= 1 && __atomic_compare_exchange_n(lock, &v, 2, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) ? 0 : 1;
}
void LightLock_Unlock(LightLock* lock) {
    if (__atomic_exchange_n(lock, 1, __ATOMIC_RELEASE) == 3) futex(lock, FUTEX_WAKE, 1);
}

static u32 selfTag() { return u32(syscall(SYS_gettid)); }
void RecursiveLock_Init(RecursiveLock* lock) { LightLock_Init(&lock->lock); lock->thread_tag = 0; lock->counter = 0; }
void RecursiveLock_Lock(RecursiveLock* lock) {
    const u32 tag = selfTag();
    if (__atomic_load_n(&lock->thread_tag, __ATOMIC_ACQUIRE) != tag) {
        LightLock_Lock(&lock->lock);
        __atomic_store_n(&lock->thread_tag, tag, __ATOMIC_RELEASE);
    }
    lock->counter++;
}
int RecursiveLock_TryLock(RecursiveLock* lock) {
    const u32 tag = selfTag();
    if (__atomic_load_n(&lock->thread_tag, __ATOMIC_ACQUIRE) != tag) {
        if (LightLock_TryLock(&lock->lock)) return 1;
        __atomic_store_n(&lock->thread_tag, tag, __ATOMIC_RELEASE);
    }
    lock->counter++;
    return 0;
}
void RecursiveLock_Unlock(RecursiveLock* lock) {
    if (--lock->counter == 0) {
        __atomic_store_n(&lock->thread_tag, 0u, __ATOMIC_RELEASE);
        LightLock_Unlock(&lock->lock);
    }
}

// LightEvent: state -2 cleared sticky, -1 cleared oneshot, 0 signaled oneshot, 1 signaled sticky.
void LightEvent_Init(LightEvent* ev, ResetType type) {
    LightLock_Init(&ev->lock);
    __atomic_store_n(&ev->state, type == RESET_STICKY ? -2 : -1, __ATOMIC_RELEASE);
}
void LightEvent_Clear(LightEvent* ev) {
    s32 s = __atomic_load_n(&ev->state, __ATOMIC_ACQUIRE);
    if (s == 1) __atomic_store_n(&ev->state, -2, __ATOMIC_RELEASE);
    else if (s == 0) __atomic_store_n(&ev->state, -1, __ATOMIC_RELEASE);
}
void LightEvent_Signal(LightEvent* ev) {
    s32 s = __atomic_load_n(&ev->state, __ATOMIC_ACQUIRE);
    if (s == -1 || s == 0) { __atomic_store_n(&ev->state, 0, __ATOMIC_RELEASE); futex(&ev->state, FUTEX_WAKE, 1); }
    else if (s == -2 || s == 1) { __atomic_store_n(&ev->state, 1, __ATOMIC_RELEASE); futex(&ev->state, FUTEX_WAKE, INT_MAX); }
}
void LightEvent_Pulse(LightEvent* ev) { LightEvent_Signal(ev); LightEvent_Clear(ev); }
int LightEvent_TryWait(LightEvent* ev) {
    s32 s = __atomic_load_n(&ev->state, __ATOMIC_ACQUIRE);
    if (s == 1) return 1;
    if (s == 0) { s32 e = 0; return __atomic_compare_exchange_n(&ev->state, &e, -1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 1 : 0; }
    return 0;
}
static int eventWait(LightEvent* ev, s64 timeout_ns) {
    const u64 deadline = timeout_ns < 0 ? 0 : monoNs() + u64(timeout_ns);
    for (;;) {
        if (LightEvent_TryWait(ev)) return 0;
        s32 s = __atomic_load_n(&ev->state, __ATOMIC_ACQUIRE);
        if (s >= 0) continue;
        if (timeout_ns < 0) futex(&ev->state, FUTEX_WAIT, s);
        else {
            const u64 now = monoNs();
            if (now >= deadline) return 1;
            struct timespec ts = nsToTs(s64(deadline - now));
            futex(&ev->state, FUTEX_WAIT, s, &ts);
        }
    }
}
void LightEvent_Wait(LightEvent* ev) { eventWait(ev, -1); }
int LightEvent_WaitTimeout(LightEvent* ev, s64 timeout_ns) { return eventWait(ev, timeout_ns); }

// ---------------------------------------------------------------- linear / vram memory
// LINEAR heap: ordinary memory with a size header. The game wraps these
// (--wrap) with its own lock and calls the definitions here as __real_*.
namespace { struct alignas(16) LinHdr { void* base; size_t size; u32 magic; }; }
static std::atomic<size_t> linearUsed{0};
// The 3DS maps two read-only config pages at fixed addresses, and libctru's
// inline helpers (osGet3DSliderState, osGetKernelVersion, ...) read them
// directly. Provide them at the same addresses, filled like a New 3DS on
// 11.17 with the 3D slider at 0.
__attribute__((constructor)) static void mapConfigPages() {
    void* p = mmap((void*)uintptr_t(OS_KERNELCFG_VADDR), 0x2000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void*)uintptr_t(OS_KERNELCFG_VADDR)) {
        fprintf(stderr, "[os] cannot map config pages at 0x%08x\n", unsigned(OS_KERNELCFG_VADDR));
        return;
    }
    auto* k = const_cast<osKernelConfig_s*>(OS_KernelConfig);
    k->kernel_ver = k->firm_ver = SYSTEM_VERSION(2, 58, 0);
    k->kernel_syscore_ver = k->firm_syscore_ver = 2;
    k->app_memtype = 6; // New 3DS, 124 MiB application region
    k->memregion_sz[0] = 0x07C00000;
    k->memregion_sz[1] = 0x00C00000; // SYSTEM
    k->memregion_sz[2] = 0x02000000; // BASE
    auto* s = const_cast<osSharedConfig_s*>(OS_SharedConfig);
    s->running_hw = 1; // retail
    s->wifi_strength = 0;
    s->slider_3d = 0.f;
    s->led_3d = 1; // LED off
    s->headset_connected = 0;
    mprotect(p, 0x2000, PROT_READ);
}

static size_t linearBudget() { static size_t b = [] { const char* e = getenv("MKW_LINEAR_MB"); return size_t(e ? atoi(e) : 64) << 20; }(); return b; }
static LinHdr* hdrOf(void* p) { return reinterpret_cast<LinHdr*>(p) - 1; }
void* linearMemAlign(size_t size, size_t alignment) {
    if (alignment < 16) alignment = 16;
    if (alignment & (alignment - 1)) return nullptr;
    if (linearUsed + size > linearBudget()) return nullptr;
    void* base = malloc(size + alignment + sizeof(LinHdr));
    if (!base) return nullptr;
    uintptr_t p = (uintptr_t(base) + sizeof(LinHdr) + alignment - 1) & ~uintptr_t(alignment - 1);
    LinHdr* h = reinterpret_cast<LinHdr*>(p) - 1;
    h->base = base; h->size = size; h->magic = 0x4C494E52;
    linearUsed += size;
    return reinterpret_cast<void*>(p);
}
void* linearAlloc(size_t size) { return linearMemAlign(size, 0x80); }
size_t linearGetSize(void* mem) { return mem && hdrOf(mem)->magic == 0x4C494E52 ? hdrOf(mem)->size : 0; }
void linearFree(void* mem) {
    if (!mem) return;
    LinHdr* h = hdrOf(mem);
    if (h->magic != 0x4C494E52) return;
    h->magic = 0;
    linearUsed -= h->size;
    free(h->base);
}
void* linearRealloc(void* mem, size_t size) {
    if (!mem) return linearAlloc(size);
    void* n = linearAlloc(size);
    if (!n) return nullptr;
    size_t old = linearGetSize(mem);
    memcpy(n, mem, old < size ? old : size);
    linearFree(mem);
    return n;
}
u32 linearSpaceFree(void) { size_t u = linearUsed; return u32(u < linearBudget() ? linearBudget() - u : 0); }

// VRAM: 6 MiB of ordinary memory mapped at the 3DS VRAM virtual address, so
// the game's address-range checks (renderer.cpp moveToVram) hold. The GL
// driver owns real video memory; this is the CPU-side copy. First-fit list.
namespace { struct VBlock { uintptr_t at; size_t size; }; }
static std::mutex vramLock;
static std::vector<VBlock>* vramUsedList = nullptr;
static uintptr_t vramBase = 0;
static size_t vramUsed = 0;
static bool vramMap() {
    if (vramBase) return true;
    void* p = mmap((void*)uintptr_t(OS_VRAM_VADDR), OS_VRAM_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || uintptr_t(p) != uintptr_t(OS_VRAM_VADDR)) {
        if (p != MAP_FAILED) munmap(p, OS_VRAM_SIZE);
        p = mmap(nullptr, OS_VRAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return false;
    }
    vramBase = uintptr_t(p);
    vramUsedList = new std::vector<VBlock>;
    return true;
}
void* vramMemAlign(size_t size, size_t alignment) {
    std::lock_guard<std::mutex> g(vramLock);
    if (!size || !vramMap()) return nullptr;
    if (alignment < 0x80) alignment = 0x80;
    size = (size + 0x7F) & ~size_t(0x7F);
    uintptr_t cur = vramBase;
    auto& list = *vramUsedList;
    size_t i = 0;
    for (;; ++i) {
        cur = (cur + alignment - 1) & ~uintptr_t(alignment - 1);
        const uintptr_t limit = i < list.size() ? list[i].at : vramBase + OS_VRAM_SIZE;
        if (cur + size <= limit) break;
        if (i >= list.size()) return nullptr;
        cur = list[i].at + list[i].size;
    }
    list.insert(list.begin() + i, VBlock{cur, size});
    vramUsed += size;
    return (void*)cur;
}
void* vramAlloc(size_t size) { return vramMemAlign(size, 0x80); }
static VBlock* vramFind(void* mem) {
    if (!vramUsedList) return nullptr;
    for (auto& b : *vramUsedList) if (b.at == uintptr_t(mem)) return &b;
    return nullptr;
}
void vramFree(void* mem) {
    std::lock_guard<std::mutex> g(vramLock);
    VBlock* b = vramFind(mem);
    if (!b) return;
    vramUsed -= b->size;
    vramUsedList->erase(vramUsedList->begin() + (b - vramUsedList->data()));
}
size_t vramGetSize(void* mem) { std::lock_guard<std::mutex> g(vramLock); VBlock* b = vramFind(mem); return b ? b->size : 0; }
u32 vramSpaceFree(void) { std::lock_guard<std::mutex> g(vramLock); return u32(OS_VRAM_SIZE - vramUsed); }

// ---------------------------------------------------------------- APT
static std::atomic<bool> quitRequested{false};
static aptHookCookie* hooks = nullptr;
static std::mutex hookLock;
static void onSignal(int sig) {
    static const char msg[] = "apt: quit signal\n";
    if (write(2, msg, sizeof msg - 1) < 0) {}
    (void)sig;
    quitRequested = true;
}
static void installSignals() {
    static bool done = false;
    if (done) return;
    done = true;
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
}
void portRequestQuit(void) { quitRequested = true; }
bool aptMainLoop(void) {
    installSignals();
    if (!quitRequested) return true;
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        std::lock_guard<std::mutex> g(hookLock);
        for (aptHookCookie* c = hooks; c; c = c->next) if (c->callback) c->callback(APTHOOK_ONEXIT, c->param);
    }
    return false;
}
void aptHook(aptHookCookie* cookie, aptHookFn callback, void* param) {
    std::lock_guard<std::mutex> g(hookLock);
    cookie->callback = callback; cookie->param = param; cookie->next = hooks; hooks = cookie;
}
void aptUnhook(aptHookCookie* cookie) {
    std::lock_guard<std::mutex> g(hookLock);
    for (aptHookCookie** c = &hooks; *c; c = &(*c)->next) if (*c == cookie) { *c = cookie->next; break; }
}
// Cortex-A7 quad core: report New 3DS so the game uses its faster paths.
Result APT_CheckNew3DS(bool* out) { if (out) *out = true; return 0; }
static u32 cpuLimit = 80;
Result APT_GetAppCpuTimeLimit(u32* percent) { if (percent) *percent = cpuLimit; return 0; }
Result APT_SetAppCpuTimeLimit(u32 percent) { cpuLimit = percent; return 0; }

// ---------------------------------------------------------------- CFGU
Result cfguInit(void) { return 0; }
void cfguExit(void) {}
Result CFGU_GetSystemModel(u8* model) { if (model) *model = CFG_MODEL_N3DSXL; return 0; }
Result CFGU_GetModelNintendo2DS(u8* value) { if (value) *value = 1; return 0; }
Result CFGU_GenHashConsoleUnique(u32 salt, u64* hash) {
    u64 h = 1469598103934665603ULL ^ salt;
    if (FILE* f = fopen("/etc/machine-id", "r")) { int c; while ((c = fgetc(f)) != EOF) { h ^= u64(c); h *= 1099511628211ULL; } fclose(f); }
    if (hash) *hash = h;
    return 0;
}
Result CFGU_GetConfigInfoBlk2(u32 size, u32 blkID, void* out) {
    if (!out) return -1;
    memset(out, 0, size);
    if (blkID == 0x000A0000) { // username, UTF-16
        const char* name = getenv("MKW_USERNAME"); if (!name) name = "Player";
        u16* o = static_cast<u16*>(out);
        for (u32 i = 0; name[i] && (i + 1) * 2 < size; ++i) o[i] = u8(name[i]);
    } else if (blkID == 0x000A0002 && size >= 1) { // language
        static_cast<u8*>(out)[0] = CFG_LANGUAGE_EN;
    } else if (blkID == 0x000B0000 && size >= 4) { // country/region
        static_cast<u8*>(out)[3] = 49; // USA
    }
    return 0;
}

// ---------------------------------------------------------------- cache maintenance
void portMarkDirty(const void* addr, size_t size); // c3d backend: CPU writes the GPU may read
Result GSPGPU_FlushDataCache(const void* adr, u32 size) { portMarkDirty(adr, size); return 0; }
Result GSPGPU_InvalidateDataCache(const void*, u32) { return 0; }
Result DSP_FlushDataCache(const void*, u32) { return 0; }

// ---------------------------------------------------------------- UTF
ssize_t utf8_to_utf16(uint16_t* out, const uint8_t* in, size_t len) {
    ssize_t n = 0;
    while (*in) {
        u32 c = *in++;
        if (c >= 0xF0) { c = (c & 7) << 18; for (int i = 2; i >= 0 && *in; --i) c |= u32(*in++ & 0x3F) << (6 * i); }
        else if (c >= 0xE0) { c = (c & 15) << 12; for (int i = 1; i >= 0 && *in; --i) c |= u32(*in++ & 0x3F) << (6 * i); }
        else if (c >= 0xC0) { c = (c & 31) << 6; if (*in) c |= *in++ & 0x3F; }
        if (c >= 0x10000) {
            c -= 0x10000;
            if (out && size_t(n) < len) out[n] = u16(0xD800 | (c >> 10));
            ++n;
            if (out && size_t(n) < len) out[n] = u16(0xDC00 | (c & 0x3FF));
            ++n;
        } else {
            if (out && size_t(n) < len) out[n] = u16(c);
            ++n;
        }
    }
    return n;
}

// ---------------------------------------------------------------- applets (no system UI on the console)
void swkbdInit(SwkbdState* swkbd, SwkbdType type, int numButtons, int maxTextLength) {
    memset(swkbd, 0, sizeof(*swkbd));
    swkbd->type = type; swkbd->num_buttons_m1 = numButtons - 1; swkbd->max_text_len = maxTextLength;
}
void swkbdSetButton(SwkbdState*, SwkbdButton, const char*, bool) {}
void swkbdSetHintText(SwkbdState*, const char*) {}
static thread_local std::string swkbdInitial;
void swkbdSetInitialText(SwkbdState*, const char* text) { swkbdInitial = text ? text : ""; }
void swkbdSetFilterCallback(SwkbdState*, SwkbdCallbackFn, void*) {}
SwkbdButton swkbdInputText(SwkbdState*, char* buf, size_t bufsize) {
    // No on-screen keyboard on the console: accept the suggested text, or
    // "Player" when there is none (only used for the license name), so
    // creating a license doesn't get cancelled.
    const std::string text = swkbdInitial.empty() ? "Player" : swkbdInitial;
    if (buf && bufsize) { strncpy(buf, text.c_str(), bufsize - 1); buf[bufsize - 1] = 0; }
    fprintf(stderr, "applet: keyboard -> \"%s\"\n", text.c_str());
    return SWKBD_BUTTON_RIGHT;
}
void miiSelectorInit(MiiSelectorConf* conf) { memset(conf, 0, sizeof(*conf)); }
void miiSelectorSetTitle(MiiSelectorConf*, const char*) {}
void miiSelectorSetOptions(MiiSelectorConf*, u32) {}
// There is no system Mii Selector on the console, and answering "cancelled"
// made New License bounce straight back. Instead hand out a Mii: the one in
// <sdmc>/3ds/mkw-3ds/default-mii.bin (raw 0x5C-byte 3DS MiiData) if present,
// else blank appearance data (version 3, like the game's own QA stand-in).
// A blank Mii gets fresh ID bytes each time, so licenses don't share one.
void miiSelectorLaunch(const MiiSelectorConf*, MiiSelectorReturn* ret) {
    memset(ret, 0, sizeof(*ret));
    ret->guest_mii_index = 0xFFFFFFFF;
    u8* m = reinterpret_cast<u8*>(&ret->mii);
    bool loaded = false;
    if (FILE* f = fopen("sdmc:/3ds/mkw-3ds/default-mii.bin", "rb")) {
        loaded = fread(m, 1, sizeof ret->mii, f) == sizeof ret->mii && m[0] == 3;
        fclose(f);
    }
    if (!loaded) {
        memset(m, 0, sizeof ret->mii);
        m[0] = 3;
        timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        const u32 id = u32(ts.tv_sec) ^ u32(ts.tv_nsec);
        m[4] = u8(id >> 24); m[5] = u8(id >> 16); m[6] = u8(id >> 8); m[7] = u8(id);
        m[12] = 0x80; m[13] = u8(id); m[14] = u8(id >> 8); m[15] = u8(id >> 16);
    }
    fprintf(stderr, "applet: Mii Selector -> %s Mii\n", loaded ? "default-mii.bin" : "blank");
}
bool miiSelectorChecksumIsValid(const MiiSelectorReturn*) { return true; }
void miiSelectorReturnGetName(const MiiSelectorReturn*, char* out, size_t max) { if (out && max) out[0] = 0; }

// ---------------------------------------------------------------- console (stdout)
static PrintConsole consoleState;
PrintConsole* consoleInit(gfxScreen_t screen, PrintConsole* console) {
    // libctru switches the LCD to RGB565 single-buffered; text goes to stdout.
    gfxSetScreenFormat(screen, GSP_RGB565_OES);
    gfxSetDoubleBuffering(screen, false);
    return console ? console : &consoleState;
}
void consoleClear(void) { fputs("\n", stdout); fflush(stdout); }

// ---------------------------------------------------------------- romfs / FS
Result romfsMountSelf(const char*) {
    struct stat st;
    return (stat(port::romfsRoot().c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : (Result)0xC8804464;
}
Result romfsMountFromTitle(u64, FS_MediaType, const char*) { return (Result)0xC8804464; }
Result romfsUnmount(const char*) { return 0; }
FS_Path fsMakePath(FS_PathType type, const void* path) {
    FS_Path p = {type, 0, path};
    if (type == PATH_ASCII) p.size = strlen((const char*)path) + 1;
    else if (type == PATH_UTF16) { const u16* s = (const u16*)path; u32 n = 0; while (s[n]) ++n; p.size = (n + 1) * 2; }
    else if (type == PATH_EMPTY) p.size = 0;
    return p;
}
// Shared extdata (system Mii database) does not exist on this console.
Result FSUSER_OpenArchive(FS_Archive*, FS_ArchiveID, FS_Path) { return (Result)0xC8804478; }
Result FSUSER_CloseArchive(FS_Archive) { return 0; }
Result FSUSER_OpenFile(Handle*, FS_Archive, FS_Path, u32, u32) { return (Result)0xC8804478; }
Result FSFILE_Read(Handle, u32* n, u64, void*, u32) { if (n) *n = 0; return (Result)0xC8804478; }
Result FSFILE_Write(Handle, u32* n, u64, const void*, u32, u32) { if (n) *n = 0; return (Result)0xC8804478; }
Result FSFILE_Close(Handle) { return 0; }

} // extern "C"

// ---------------------------------------------------------------- path translation
namespace port {
static std::string exeDir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    char* slash = strrchr(buf, '/');
    if (slash) *slash = 0;
    return buf;
}
const std::string& sdmcRoot() {
    static const std::string r = [] { const char* e = getenv("MKW_SDMC"); return e ? std::string(e) : exeDir() + "/sdmc"; }();
    return r;
}
const std::string& romfsRoot() {
    static const std::string r = [] { const char* e = getenv("MKW_ROMFS"); return e ? std::string(e) : exeDir() + "/romfs"; }();
    return r;
}
// Returns path unchanged unless it uses a 3DS device prefix.
const char* translate(const char* path, std::string& storage) {
    if (!path) return path;
    const std::string* root = nullptr;
    size_t skip = 0;
    if (!strncmp(path, "sdmc:", 5)) { root = &sdmcRoot(); skip = 5; }
    else if (!strncmp(path, "romfs:", 6)) { root = &romfsRoot(); skip = 6; }
    if (!root) return path;
    storage = *root;
    if (path[skip] != '/') storage += '/';
    storage += path + skip;
    return storage.c_str();
}
}

// Interpose the libc entry points the game uses with device paths. The real
// functions come from the next object in link order (glibc).
template <class F> static F nextSym(const char* name) {
    static_assert(sizeof(F) == sizeof(void*));
    void* p = dlsym(RTLD_NEXT, name);
    if (!p) { fprintf(stderr, "[os] libc has no %s\n", name); abort(); }
    F f; memcpy(&f, &p, sizeof f); return f;
}
#define PORT_NEXT(name) static auto real = nextSym<decltype(&::name)>(#name)
extern "C" {
FILE* fopen(const char* path, const char* mode) { PORT_NEXT(fopen); std::string s; return real(port::translate(path, s), mode); }
FILE* freopen(const char* path, const char* mode, FILE* f) { PORT_NEXT(freopen); std::string s; return real(port::translate(path, s), mode, f); }
int remove(const char* path) { PORT_NEXT(remove); std::string s; return real(port::translate(path, s)); }
int rename(const char* a, const char* b) { PORT_NEXT(rename); std::string s, t; return real(port::translate(a, s), port::translate(b, t)); }
int unlink(const char* path) { PORT_NEXT(unlink); std::string s; return real(port::translate(path, s)); }
int rmdir(const char* path) { PORT_NEXT(rmdir); std::string s; return real(port::translate(path, s)); }
int mkdir(const char* path, mode_t m) { PORT_NEXT(mkdir); std::string s; return real(port::translate(path, s), m); }
int chdir(const char* path) { PORT_NEXT(chdir); std::string s; return real(port::translate(path, s)); }
int access(const char* path, int m) { PORT_NEXT(access); std::string s; return real(port::translate(path, s), m); }
#if defined(__arm__)
int portRawStat(const char* path, struct stat* st); // port/compat/glibc_compat.c (console glibc 2.22)
int stat(const char* __restrict path, struct stat* __restrict st) { std::string s; return portRawStat(port::translate(path, s), st); }
#else
int stat(const char* __restrict path, struct stat* __restrict st) { PORT_NEXT(stat); std::string s; return real(port::translate(path, s), st); }
#endif
DIR* opendir(const char* path) { PORT_NEXT(opendir); std::string s; return real(port::translate(path, s)); }
int open(const char* path, int flags, ...) {
    PORT_NEXT(open);
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    std::string s;
    return real(port::translate(path, s), flags, mode);
}
}
