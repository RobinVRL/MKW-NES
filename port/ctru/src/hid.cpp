// HID on Linux evdev: every /dev/input/event* (Classic controllers via the
// NES Mini's clvcon driver, USB pads, keyboards) folds into one 3DS key state.
#include <cerrno>
#include <cstdint>
typedef uint32_t u32; typedef int16_t s16; typedef int32_t Result;
struct circlePosition { s16 dx, dy; };
struct angularRate { s16 x, z, y; };
// 3DS HID bits (libctru hid.h); named K3_* because linux/input.h owns KEY_*.
enum : u32 { K3_A = 1u << 0, K3_B = 1u << 1, K3_SELECT = 1u << 2, K3_START = 1u << 3, K3_DRIGHT = 1u << 4, K3_DLEFT = 1u << 5, K3_DUP = 1u << 6, K3_DDOWN = 1u << 7,
  K3_R = 1u << 8, K3_L = 1u << 9, K3_X = 1u << 10, K3_Y = 1u << 11, K3_ZL = 1u << 14, K3_ZR = 1u << 15,
  K3_CPAD_RIGHT = 1u << 28, K3_CPAD_LEFT = 1u << 29, K3_CPAD_UP = 1u << 30, K3_CPAD_DOWN = 1u << 31 };
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <mutex>
// Not <poll.h>: that resolves to the 3DS stub in port/ctru/include, whose
// struct pollfd (12 bytes) and POLL* values don't match Linux.
#include <sys/poll.h>
static_assert(sizeof(pollfd) == 8 && POLLHUP == 0x10, "need the Linux pollfd");
#include <sys/ioctl.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>

extern "C" void portRequestQuit(void);

namespace {
// The event layout the kernel actually writes. The build uses 64-bit time_t,
// which makes <linux/input.h>'s input_event 24 bytes, but this 32-bit kernel
// (3.4) always sends 16-byte events with 32-bit seconds/microseconds.
struct KernelEvent { uint32_t sec, usec; uint16_t type, code; int32_t value; };
static_assert(sizeof(KernelEvent) == 16, "kernel input_event is 16 bytes on 32-bit ARM");
struct Device { int fd; int absMin[ABS_CNT]; int absMax[ABS_CNT]; };
std::mutex lock;
u32 liveKeys = 0;          // from buttons/keys
u32 hatKeys = 0;           // from ABS_HAT0
int stickX = 0, stickY = 0; // circle pad, -156..156
u32 held = 0, down = 0, up = 0, repeat = 0;
u32 repeatDelay = 30, repeatInterval = 6, repeatCount = 0;
circlePosition circle{};
std::atomic<bool> started{false};

u32 mapKey(int code) {
    switch (code) {
    // Gamepads: the NES/SNES Classic pads send A as BTN_A (south) and B as
    // BTN_B (east); padsend.py sends Cross as south so it is A too.
    case BTN_SOUTH: return K3_A;
    case BTN_EAST: return K3_B;
    case BTN_NORTH: return K3_X;
    case BTN_WEST: return K3_Y;
    case BTN_TL: return K3_L;
    case BTN_TR: return K3_R;
    case BTN_TL2: return K3_ZL;
    case BTN_TR2: return K3_ZR;
    case BTN_SELECT: return K3_SELECT;
    case BTN_START: return K3_START;
    case BTN_DPAD_UP: return K3_DUP;
    case BTN_DPAD_DOWN: return K3_DDOWN;
    case BTN_DPAD_LEFT: return K3_DLEFT;
    case BTN_DPAD_RIGHT: return K3_DRIGHT;
    // Keyboard (development).
    case KEY_X: return K3_A;
    case KEY_Z: return K3_B;
    case KEY_S: return K3_X;
    case KEY_A: return K3_Y;
    case KEY_Q: return K3_L;
    case KEY_W: return K3_R;
    case KEY_ENTER: return K3_START;
    case KEY_RIGHTSHIFT: return K3_SELECT;
    case KEY_UP: return K3_DUP;
    case KEY_DOWN: return K3_DDOWN;
    case KEY_LEFT: return K3_DLEFT;
    case KEY_RIGHT: return K3_DRIGHT;
    default: return 0;
    }
}

int scaleAxis(const Device& d, int code, int v) {
    const int lo = d.absMin[code], hi = d.absMax[code];
    if (hi <= lo) return 0;
    const int mid = (lo + hi) / 2, half = (hi - lo) / 2;
    int s = (v - mid) * 156 / (half ? half : 1);
    if (s > -16 && s < 16) s = 0; // dead zone
    return s < -156 ? -156 : s > 156 ? 156 : s;
}

const bool hidLog = getenv("MKW_HIDLOG") && *getenv("MKW_HIDLOG");

// Opens /dev/input/event* nodes not open yet. Controllers can appear at any
// time (NES pad plugged in, PC bridge started after the game), so this runs
// every couple of seconds.
void scanDevices(std::vector<Device>& devs, std::vector<pollfd>& fds, std::vector<int>& nums) {
    DIR* dir = opendir("/dev/input");
    if (!dir) return;
    while (dirent* e = readdir(dir)) {
        if (strncmp(e->d_name, "event", 5)) continue;
        const int num = atoi(e->d_name + 5);
        bool have = false;
        for (size_t i = 0; i < nums.size(); ++i) have |= nums[i] == num && fds[i].fd >= 0;
        if (have) continue;
        char path[300];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        Device d{fd, {}, {}};
        for (int a = 0; a < ABS_CNT; ++a) {
            input_absinfo info{};
            if (ioctl(fd, EVIOCGABS(a), &info) == 0) { d.absMin[a] = info.minimum; d.absMax[a] = info.maximum; }
        }
        char name[128] = "?";
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        fprintf(stderr, "hid: opened %s (%s)\n", path, name);
        // Reuse the slot of a device that went away.
        size_t slot = 0;
        while (slot < fds.size() && fds[slot].fd >= 0) ++slot;
        if (slot == fds.size()) { devs.push_back(d); fds.push_back({fd, POLLIN, 0}); nums.push_back(num); }
        else { devs[slot] = d; fds[slot] = {fd, POLLIN, 0}; nums[slot] = num; }
    }
    closedir(dir);
}

void* reader(void*) {
    std::vector<Device> devs;
    std::vector<pollfd> fds;
    std::vector<int> nums;
    scanDevices(devs, fds, nums);
    unsigned idle = 0;
    for (;;) {
        if (++idle >= 4 || fds.empty()) { idle = 0; scanDevices(devs, fds, nums); }
        if (fds.empty()) { poll(nullptr, 0, 500); continue; }
        if (poll(fds.data(), fds.size(), 500) <= 0) continue;
        for (size_t i = 0; i < fds.size(); ++i) {
            // A device that errors or goes away (e.g. the PC bridge closing)
            // would make poll() return at once forever; stop watching it.
            if (fds[i].fd >= 0 && (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                fprintf(stderr, "hid: event%d went away\n", nums[i]);
                close(fds[i].fd); fds[i].fd = -1;
                std::lock_guard<std::mutex> g(lock);
                liveKeys = hatKeys = 0; stickX = stickY = 0; // don't leave keys stuck
                continue;
            }
            if (!(fds[i].revents & POLLIN)) continue;
            KernelEvent ev[32];
            ssize_t n = read(fds[i].fd, ev, sizeof ev);
            if (n < 0 && errno != EAGAIN && errno != EINTR) { close(fds[i].fd); fds[i].fd = -1; continue; }
            if (n <= 0) continue;
            std::lock_guard<std::mutex> g(lock);
            for (ssize_t k = 0; k < n / ssize_t(sizeof(KernelEvent)); ++k) {
                const KernelEvent& e = ev[k];
                if (hidLog && e.type == EV_KEY) fprintf(stderr, "hid: event%d key 0x%x = %d\n", nums[i], e.code, e.value);
                if (e.type == EV_KEY) {
                    if (e.code == BTN_MODE || e.code == KEY_ESC) {
                        if (e.value) { fprintf(stderr, "hid: quit key %d value %d on device %zu\n", e.code, e.value, i); portRequestQuit(); }
                        continue;
                    }
                    const u32 m = mapKey(e.code);
                    if (e.value) liveKeys |= m; else liveKeys &= ~m;
                } else if (e.type == EV_ABS) {
                    const Device& d = devs[i];
                    if (e.code == ABS_HAT0X) hatKeys = (hatKeys & ~(K3_DLEFT | K3_DRIGHT)) | (e.value < 0 ? K3_DLEFT : e.value > 0 ? K3_DRIGHT : 0);
                    else if (e.code == ABS_HAT0Y) hatKeys = (hatKeys & ~(K3_DUP | K3_DDOWN)) | (e.value < 0 ? K3_DUP : e.value > 0 ? K3_DDOWN : 0);
                    else if (e.code == ABS_X) stickX = scaleAxis(d, ABS_X, e.value);
                    else if (e.code == ABS_Y) stickY = -scaleAxis(d, ABS_Y, e.value);
                }
            }
        }
    }
    return nullptr;
}
void start() {
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) return;
    pthread_t t;
    if (pthread_create(&t, nullptr, reader, nullptr) == 0) pthread_detach(t);
}
}

// MKW_INPUT="<scan>:<KEY>[:<scans held>],..." presses keys at given
// hidScanInput counts (testing without a pad), e.g. "300:A,420:A:5,600:DDOWN".
static u32 scriptedKeys() {
    struct Press { u32 at, len, key; };
    static std::vector<Press> script = [] {
        std::vector<Press> v;
        const char* s = getenv("MKW_INPUT");
        static const struct { const char* n; u32 k; } names[] = {
            {"A", K3_A}, {"B", K3_B}, {"X", K3_X}, {"Y", K3_Y}, {"L", K3_L}, {"R", K3_R}, {"ZL", K3_ZL}, {"ZR", K3_ZR},
            {"START", K3_START}, {"SELECT", K3_SELECT}, {"DUP", K3_DUP}, {"DDOWN", K3_DDOWN}, {"DLEFT", K3_DLEFT}, {"DRIGHT", K3_DRIGHT}};
        while (s && *s) {
            char key[16] = {};
            unsigned at = 0, len = 3;
            int n = 0;
            if (sscanf(s, "%u:%15[A-Z]%n", &at, key, &n) < 2) break;
            s += n;
            if (*s == ':') { len = unsigned(strtoul(s + 1, const_cast<char**>(&s), 10)); }
            for (auto& e : names) if (!strcmp(e.n, key)) v.push_back({at, len ? len : 1, e.k});
            if (*s == ',') ++s;
        }
        return v;
    }();
    static u32 scan = 0;
    ++scan;
    u32 k = 0;
    for (const Press& p : script) if (scan >= p.at && scan < p.at + p.len) k |= p.key;
    return k;
}

extern "C" {
void hidScanInput(void) {
    start();
    std::lock_guard<std::mutex> g(lock);
    u32 now = liveKeys | hatKeys | scriptedKeys();
    // A pad without a stick drives the circle pad from the d-pad, which the
    // game reads for steering.
    int cx = stickX, cy = stickY;
    if (!cx && !cy) {
        if (now & K3_DLEFT) cx = -156; else if (now & K3_DRIGHT) cx = 156;
        if (now & K3_DUP) cy = 156; else if (now & K3_DDOWN) cy = -156;
    }
    if (cx < -40) now |= K3_CPAD_LEFT; else if (cx > 40) now |= K3_CPAD_RIGHT;
    if (cy < -40) now |= K3_CPAD_DOWN; else if (cy > 40) now |= K3_CPAD_UP;
    circle.dx = s16(cx); circle.dy = s16(cy);
    down = now & ~held; up = held & ~now;
    if (down) { repeatCount = repeatDelay; repeat = down; }
    else if (now && repeatCount && !--repeatCount) { repeatCount = repeatInterval; repeat = now; }
    else repeat = 0;
    held = now;
}
u32 hidKeysHeld(void) { return held; }
u32 hidKeysDown(void) { return down; }
u32 hidKeysUp(void) { return up; }
u32 hidKeysDownRepeat(void) { return repeat | down; }
void hidSetRepeatParameters(u32 delay, u32 interval) { repeatDelay = delay ? delay : 1; repeatInterval = interval ? interval : 1; }
void hidCircleRead(circlePosition* pos) { if (pos) *pos = circle; }
void hidGyroRead(angularRate* rate) { if (rate) memset(rate, 0, sizeof(*rate)); }
Result HIDUSER_EnableGyroscope(void) { return 0; }
Result HIDUSER_DisableGyroscope(void) { return 0; }
Result HIDUSER_GetGyroscopeRawToDpsCoefficient(float* coeff) { if (coeff) *coeff = 14.375f; return 0; }
}
