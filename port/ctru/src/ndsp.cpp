// NDSP timing model without an audio device. Wave buffers are consumed at
// their channel rate in DSP frames (160 samples at 32728.498 Hz) and the frame
// callback runs, so streaming and sound-completion logic behave as on the 3DS.
// Output is silent; an ALSA mixer can be added behind this same interface.
#include <3ds.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <time.h>

namespace {
constexpr int Channels = 24;
constexpr double DspRate = 32728.498046875;
constexpr int FrameSamples = 160;
struct Channel {
    ndspWaveBuf* queue = nullptr;
    double pos = 0, rate = DspRate;
    bool paused = false;
    u16 seq = 0;
};
std::mutex lock;
Channel chn[Channels];
ndspCallback callback = nullptr;
void* callbackData = nullptr;
std::atomic<u32> frames{0};
std::atomic<bool> running{false};
pthread_t thread;

void advance(Channel& c) {
    if (c.paused || !c.queue) return;
    c.pos += c.rate * FrameSamples / DspRate;
    while (c.queue) {
        ndspWaveBuf* b = c.queue;
        b->status = NDSP_WBUF_PLAYING;
        if (c.pos < double(b->nsamples)) break;
        if (b->looping) { c.pos = std::fmod(c.pos, double(b->nsamples ? b->nsamples : 1)); break; }
        c.pos -= b->nsamples;
        b->status = NDSP_WBUF_DONE;
        c.queue = b->next;
    }
    if (!c.queue) c.pos = 0;
}
void* run(void*) {
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    const long periodNs = long(1e9 * FrameSamples / DspRate);
    while (running) {
        {
            std::lock_guard<std::mutex> g(lock);
            for (auto& c : chn) advance(c);
        }
        frames++;
        if (callback) callback(callbackData);
        next.tv_nsec += periodNs;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
    }
    return nullptr;
}
}

extern "C" {
Result ndspInit(void) {
    bool expected = false;
    if (running.compare_exchange_strong(expected, true)) pthread_create(&thread, nullptr, run, nullptr);
    return 0;
}
void ndspExit(void) {
    bool expected = true;
    if (running.compare_exchange_strong(expected, false)) pthread_join(thread, nullptr);
}
u32 ndspGetFrameCount(void) { return frames; }
void ndspSetCallback(ndspCallback cb, void* data) { callbackData = data; callback = cb; }
void ndspSetOutputMode(ndspOutputMode) {}
void ndspSetClippingMode(ndspClippingMode) {}

void ndspChnReset(int id) {
    std::lock_guard<std::mutex> g(lock);
    for (ndspWaveBuf* b = chn[id].queue; b; b = b->next) b->status = NDSP_WBUF_DONE;
    chn[id] = Channel{};
}
void ndspChnSetInterp(int, ndspInterpType) {}
void ndspChnSetRate(int id, float rate) { std::lock_guard<std::mutex> g(lock); chn[id].rate = rate; }
void ndspChnSetFormat(int, u16) {}
void ndspChnSetMix(int, float[12]) {}
void ndspChnSetAdpcmCoefs(int, u16[16]) {}
void ndspChnSetPaused(int id, bool paused) { std::lock_guard<std::mutex> g(lock); chn[id].paused = paused; }
void ndspChnIirMonoSetEnable(int, bool) {}
bool ndspChnIirMonoSetParamsLowPassFilter(int, float) { return true; }
void ndspChnWaveBufClear(int id) {
    std::lock_guard<std::mutex> g(lock);
    for (ndspWaveBuf* b = chn[id].queue; b; b = b->next) b->status = NDSP_WBUF_DONE;
    chn[id].queue = nullptr; chn[id].pos = 0;
}
void ndspChnWaveBufAdd(int id, ndspWaveBuf* buf) {
    std::lock_guard<std::mutex> g(lock);
    buf->next = nullptr;
    buf->status = NDSP_WBUF_QUEUED;
    buf->sequence_id = ++chn[id].seq;
    ndspWaveBuf** tail = &chn[id].queue;
    while (*tail) tail = &(*tail)->next;
    *tail = buf;
}
}
