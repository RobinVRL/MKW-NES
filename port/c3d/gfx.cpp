// libctru gfx + GSP vblank on Linux. The LCD framebuffers are CPU memory in
// the 3DS layout (pixel (x,y) at column x, index 239-y, in the screen's
// GSP format). Whatever reaches them - C3D_FrameEnd transfers or the game's
// own CPU drawing - is presented by drawing both on the HDMI output.
#include "backend.hpp"
#include "gl.hpp"
#include "port_gfx.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <time.h>

using namespace gl;

namespace {
struct Screen {
    u8* buf[2][2] = {}; // [side][index]
    int back = 0, shown = 0;
    bool doubleBuf = true;
    GSPGPU_FramebufferFormat fmt = GSP_BGR8_OES;
    int height = 400; // pixels along the long axis
    bool direct = false; // set by C3D_FrameEnd's fast path, cleared by CPU swaps
};
Screen screens[2];
bool initialized = false, stereo = false, wide = false;
std::mutex gfxLock;
std::atomic<int> pendingPresents{0};

// vblank
std::mutex vbLock;
std::condition_variable vbCv;
std::atomic<u32> vbCount{0};
std::atomic<bool> vbRun{false};
std::thread vbThread;

void vblankLoop() {
    timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    const long period = 16715000; // 59.83 Hz, the 3DS LCD rate
    while (vbRun) {
        next.tv_nsec += period;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
        { std::lock_guard<std::mutex> g(vbLock); vbCount++; }
        vbCv.notify_all();
    }
}

int bppOf(GSPGPU_FramebufferFormat f) {
    switch (f) { case GSP_RGBA8_OES: return 4; case GSP_BGR8_OES: return 3; default: return 2; }
}

void swapScreen(int s) {
    Screen& sc = screens[s];
    sc.shown = sc.back;
    if (sc.doubleBuf) sc.back ^= 1;
}

void queuePresent() {
    if (pendingPresents.load() > 2) return; // GL output is behind: drop this one
    c3d::ScreenImage img[2];
    {
        std::lock_guard<std::mutex> g(gfxLock);
        for (int s = 0; s < 2; ++s) {
            const Screen& sc = screens[s];
            img[s] = {sc.buf[0][sc.shown], int(sc.fmt), 240, sc.height, sc.direct};
        }
    }
    pendingPresents++;
    c3d::Cmd c{c3d::Cmd::Call};
    c.fn = [a = img[0], b = img[1]] { c3d::glPresent(a, b); pendingPresents--; };
    std::vector<c3d::Cmd> l;
    l.push_back(std::move(c));
    c3d::glSubmit(std::move(l));
}
}

void portWaitVBlank() {
    std::unique_lock<std::mutex> g(vbLock);
    const u32 start = vbCount.load();
    vbCv.wait_for(g, std::chrono::milliseconds(50), [&] { return vbCount.load() != start; });
}
u32 portVBlankCount() { return vbCount.load(); }

void portFrameEndPresent(bool top, bool bottom) {
    {
        std::lock_guard<std::mutex> g(gfxLock);
        if (top) swapScreen(0);
        if (bottom) swapScreen(1);
    }
    if (top || bottom) queuePresent();
}

bool portSetDirect(int screen) {
    static const bool readback = getenv("MKW_READBACK") != nullptr;
    if (readback) return false;
    std::lock_guard<std::mutex> g(gfxLock);
    screens[screen].direct = true;
    return true;
}

// ------------------------------------------------------------------ libctru API
void gfxInit(GSPGPU_FramebufferFormat topFormat, GSPGPU_FramebufferFormat bottomFormat, bool) {
    if (initialized) return;
    screens[0].fmt = topFormat;
    screens[1].fmt = bottomFormat;
    screens[1].height = 320;
    for (int s = 0; s < 2; ++s)
        for (int e = 0; e < 2; ++e)
            for (int i = 0; i < 2; ++i)
                screens[s].buf[e][i] = static_cast<u8*>(calloc(size_t(240) * screens[s].height * 4, 1));
    initialized = true;
    c3d::glStart();
    vbRun = true;
    vbThread = std::thread(vblankLoop);
}
void gfxInitDefault(void) { gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false); }
void gfxExit(void) {
    if (!initialized) return;
    c3d::glWaitIdle();
    vbRun = false;
    if (vbThread.joinable()) vbThread.join();
    initialized = false;
}
void gfxSet3D(bool enable) { stereo = enable; }
bool gfxIs3D(void) { return stereo; }
bool gfxIsWide(void) { return wide; }
void gfxSetWide(bool enable) { wide = enable; }
void gfxSetScreenFormat(gfxScreen_t screen, GSPGPU_FramebufferFormat format) {
    std::lock_guard<std::mutex> g(gfxLock);
    screens[screen == GFX_BOTTOM].fmt = format;
}
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t screen) { return screens[screen == GFX_BOTTOM].fmt; }
void gfxSetDoubleBuffering(gfxScreen_t screen, bool enable) {
    std::lock_guard<std::mutex> g(gfxLock);
    Screen& sc = screens[screen == GFX_BOTTOM];
    sc.doubleBuf = enable;
    if (!enable) sc.back = sc.shown;
}
u8* gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16* width, u16* height) {
    std::lock_guard<std::mutex> g(gfxLock);
    Screen& sc = screens[screen == GFX_BOTTOM];
    if (width) *width = 240;
    if (height) *height = u16(sc.height);
    const int e = (screen == GFX_TOP && side == GFX_RIGHT) ? 1 : 0;
    return sc.buf[e][sc.back];
}
void gfxFlushBuffers(void) {}
void gfxScreenSwapBuffers(gfxScreen_t scr, bool) {
    { std::lock_guard<std::mutex> g(gfxLock); screens[scr == GFX_BOTTOM].direct = false; swapScreen(scr == GFX_BOTTOM); }
    queuePresent();
}
void gfxConfigScreen(gfxScreen_t scr, bool) { gfxScreenSwapBuffers(scr, false); }
void gfxSwapBuffers(void) {
    { std::lock_guard<std::mutex> g(gfxLock); screens[0].direct = screens[1].direct = false; swapScreen(0); swapScreen(1); }
    queuePresent();
}
void gfxSwapBuffersGpu(void) { gfxSwapBuffers(); }

void gspWaitForEvent(GSPGPU_Event id, bool) {
    if (id == GSPGPU_EVENT_VBlank0 || id == GSPGPU_EVENT_VBlank1) { portWaitVBlank(); return; }
    c3d::flushRecorded();
    c3d::glWaitIdle(); // P3D / PPF / PSC: the GPU work they signal is done
}

void GPUCMD_Add(u32, const u32*, u32) {}

// ------------------------------------------------------------------ presentation (GL thread)
namespace c3d {
namespace {
unsigned presentProg = 0, screenTex[2] = {};
int locSwz = -1;
const char* kPresentVS = R"(
attribute vec2 aPos; attribute vec2 aUv; varying vec2 vUv;
void main() { vUv = aUv; gl_Position = vec4(aPos, 0.0, 1.0); }
)";
const char* kPresentFS = R"(
precision mediump float;
varying vec2 vUv; uniform sampler2D tex; uniform int uSwz;
void main() {
    vec4 t = texture2D(tex, vUv);
    vec3 c = uSwz == 1 ? t.bgr : uSwz == 2 ? t.wzy : t.rgb;
    gl_FragColor = vec4(c, 1.0);
}
)";

void uploadScreen(int i, const ScreenImage& img) {
    if (!screenTex[i]) {
        glGenTextures(1, &screenTex[i]);
        glBindTexture(GL_TEXTURE_2D, screenTex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, screenTex[i]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLenum f = GL_RGB, t = GL_UNSIGNED_BYTE;
    switch (img.format) {
    case GSP_RGBA8_OES: f = GL_RGBA; break;
    case GSP_BGR8_OES: break;
    case GSP_RGB565_OES: t = GL_UNSIGNED_SHORT_5_6_5; break;
    case GSP_RGB5_A1_OES: f = GL_RGBA; t = GL_UNSIGNED_SHORT_5_5_5_1; break;
    default: f = GL_RGBA; t = GL_UNSIGNED_SHORT_4_4_4_4; break;
    }
    // Texture row r = LCD column r (240 texels from the screen bottom up).
    glTexImage2D(GL_TEXTURE_2D, 0, f, img.width, img.height, 0, f, t, img.pixels);
}

// x, y, w, h in top-left window pixels
// A screen drawn straight from its render target texture (fast path): GL rows
// are the readback's rows flipped, and the colours are plain RGBA.
// Screen i (0 top, 1 bottom) is drawn from its GPU snapshot when it has one.
bool isDirect(int i, const ScreenImage& img) { return img.direct && glSnapshotTex(i) != 0; }

void drawScreen(int i, const ScreenImage& img, float x, float y, float w, float h, int winW, int winH) {
    const float x0 = x / winW * 2 - 1, x1 = (x + w) / winW * 2 - 1;
    const float y0 = 1 - y / winH * 2, y1 = 1 - (y + h) / winH * 2;
    // screen (sx right, sy down) -> texture (u = 1 - sy, v = sx)
    const bool direct = isDirect(i, img);
    // v at the screen's left / right edge. A target taller than the LCD is
    // wide (e.g. a 400-row target for the 320-wide lower screen): show only
    // the LCD's columns instead of squeezing the whole target in.
    float a = 0, b = 1;
    if (direct) {
        const int rows = glSnapshotHeight(i), lcd = i ? 320 : 400;
        a = 1; b = rows > lcd ? 1 - float(lcd) / rows : 0;
    }
    const float quad[] = {
        x0, y0, 1, a,   x1, y0, 1, b,   x0, y1, 0, a,   x1, y1, 0, b,
    };
    const int fmt = img.format;
    glBindTexture(GL_TEXTURE_2D, direct ? glSnapshotTex(i) : screenTex[i]);
    glUniform1i(locSwz, direct ? 0 : fmt == GSP_BGR8_OES ? 1 : fmt == GSP_RGBA8_OES ? 2 : 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, quad);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, quad + 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
}

void glPresent(const ScreenImage& top, const ScreenImage& bottom) {
    if (!glReady()) return;
    if (!presentProg) {
        static const char* const attribs[] = {"aPos", "aUv", nullptr};
        presentProg = glCompileProgram(kPresentVS, kPresentFS, attribs);
        if (!presentProg) return;
        glUseProgram(presentProg);
        glUniform1i(glGetUniformLocation(presentProg, "tex"), 0);
        locSwz = glGetUniformLocation(presentProg, "uSwz");
    }
    int W, H;
    glWindowSize(W, H);
// Default: the upper LCD fills the TV and the lower LCD is a small inset in
// the bottom-right corner (MKW_SHOW=pip). MKW_SHOW=top, bottom or both (side by
// side) select otherwise; MKW_PIP_SCALE sets the inset's size (fraction of
// the TV height, default 0.36).
    static const char* env = getenv("MKW_SHOW");
    static const char* show = env ? env : "pip";
    const bool pip = strcmp(show, "pip") == 0;
    const bool showTop = strcmp(show, "bottom") != 0;
    const bool showBottom = strcmp(show, "top") != 0;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, W, H);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(presentProg);
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < 6; ++i) glDisableVertexAttribArray(GLuint(i));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    if (showTop && !isDirect(0, top)) uploadScreen(0, top);
    if (showBottom && !isDirect(1, bottom)) uploadScreen(1, bottom);
    if (pip) {
        const float s = (W / 400.f < H / 240.f) ? W / 400.f : H / 240.f;
        drawScreen(0, top, (W - 400 * s) / 2, (H - 240 * s) / 2, 400 * s, 240 * s, W, H);
        static const float scale = [] {
            const char* e = getenv("MKW_PIP_SCALE");
            const float v = e ? float(atof(e)) : 0.36f;
            return v > 0.1f && v <= 1.f ? v : 0.36f;
        }();
        const float ph = H * scale, pw = ph * 320.f / 240.f;
        const float margin = H * 0.02f, border = H > 600 ? 3.f : 2.f;
        const float px = W - pw - margin, py = H - ph - margin; // bottom-right
        // Border: clear a slightly larger rectangle (GL's y axis points up).
        glEnable(GL_SCISSOR_TEST);
        glScissor(GLint(px - border), GLint(H - (py + ph + border)), GLsizei(pw + 2 * border), GLsizei(ph + 2 * border));
        glClearColor(0.85f, 0.85f, 0.85f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        drawScreen(1, bottom, px, py, pw, ph, W, H);
    } else if (showTop && showBottom) {
        // 1280x720 reference layout: top 800x480 at (0,120), bottom 480x360 at (800,180)
        const float s = (float(W) / 1280.f < float(H) / 720.f) ? float(W) / 1280.f : float(H) / 720.f;
        const float ox = (W - 1280.f * s) / 2, oy = (H - 720.f * s) / 2;
        drawScreen(0, top, ox, oy + 120 * s, 800 * s, 480 * s, W, H);
        drawScreen(1, bottom, ox + 800 * s, oy + 180 * s, 480 * s, 360 * s, W, H);
    } else {
        const int i = showTop ? 0 : 1;
        const float sw = showTop ? 400.f : 320.f, sh = 240.f;
        const float s = (W / sw < H / sh) ? W / sw : H / sh;
        drawScreen(i, showTop ? top : bottom, (W - sw * s) / 2, (H - sh * s) / 2, sw * s, sh * s, W, H);
    }
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glSwapWindow();
}
}
