#pragma once
// Shared state between the citro3d API (game threads record commands, c3d.cpp)
// and the single GL thread that executes them (glexec.cpp).
#include <citro3d.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace c3d {

// Program ids carried by the GLESDVLB tag blobs (port/gen.py).
enum Program { ProgScene = 0, ProgEffect = 1, ProgMii = 2, ProgCfl = 3, ProgCount = 4 };

// One texture's memory as the GPU sees it, found by data pointer. Commands
// hold references so deletion never races the GL thread.
struct TexMem {
    void* data = nullptr;
    u32 bytes = 0;
    u16 width = 0, height = 0;
    GPU_TEXCOLOR fmt = GPU_RGBA8;
    u8 maxLevel = 0;
    bool cube = false;
    std::atomic<u32> generation{1}; // bumped on every CPU write the game announces
    std::atomic<bool> renderTarget{false};
    // GL thread only
    unsigned glTex = 0;
    u32 uploaded = 0;
    u32 sampler = ~0u;
    int glLevels = 0;
    ~TexMem();
};
using TexRef = std::shared_ptr<TexMem>;

struct LightSnap {
    float pos[4]; // xyz, w=1 positional
    float diffuse[3], spec0[3];
};
struct LightingSnap {
    bool enabled = false;
    float ambient[3] = {};
    int count = 0;
    LightSnap light[8];
    std::shared_ptr<std::vector<float>> d0; // 256 values of the D0 LUT (null = 1.0)
    bool d0Abs = true;
};

struct DrawState {
    int program = -1;
    struct Attr { int reg; int format; int count; u32 offset; };
    struct Buf { const u8* base; int stride; int nattr; Attr attr[12]; };
    int nbuf = 0;
    Buf buf[12];
    int vp[4] = {0, 0, 240, 400};
    int scissorMode = 0, scissor[4] = {};
    bool zBuffer = true;
    float zScale = -1, zOffset = 0;
    int cull = GPU_CULL_NONE;
    bool stencil = false; int stencilFunc = GPU_ALWAYS, stencilRef = 0, stencilIn = 0xFF, stencilWrite = 0;
    int sfail = 0, dfail = 0, spass = 0;
    bool depthTest = false; int depthFunc = GPU_GEQUAL, writeMask = GPU_WRITE_ALL;
    bool alphaTest = false; int alphaFunc = GPU_ALWAYS, alphaRef = 0;
    int blendEq[2] = {GPU_BLEND_ADD, GPU_BLEND_ADD};
    int blend[4] = {GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA};
    u32 blendColor = 0;
    C3D_TexEnv env[6];
    u32 bufUpdate = 0; // rgb mask bits 8-11, alpha 12-15 (as citro3d)
    u32 bufColor = 0;
    LightingSnap lighting;
};

struct Target;

struct Cmd {
    enum Kind { Draw, Clear, Call } kind;
    // Draw
    std::shared_ptr<const DrawState> state;
    std::shared_ptr<const std::vector<float>> unif; // 96 vec4, xyzw order
    u16 bools = 0;
    int prim = 0, count = 0, first = 0, indexType = -1;
    const void* indices = nullptr;
    TexRef tex[3];
    u32 texParam[3] = {};
    // Draw / Clear
    Target* target = nullptr;
    u32 clearBits = 0, clearColor = 0, clearDepth = 0;
    // Call
    std::function<void()> fn;
};

// GL thread interface (glexec.cpp)
void glStart();
void glSubmit(std::vector<Cmd>&& list);
void glWaitIdle();
void glRun(std::function<void()> fn); // runs on the GL thread and waits
float glLastFrameMs();
void glGarbage(unsigned tex);
bool glReady();                        // GL thread: a context is current
void glWindowSize(int& w, int& h);     // GL thread
void glSwapWindow();                   // GL thread
unsigned glCompileProgram(const char* vs, const char* fs, const char* const* attribs); // GL thread

// Render target
struct Target {
    C3D_RenderTarget pub;
    TexRef color;            // the texture the target renders into (own or game's)
    int depthFmt = -1;
    int width = 0, height = 0;
    bool ownsColorMem = false;
    // GL thread only
    unsigned fbo = 0, depthRb = 0, stencilRb = 0;
};
inline Target* fromPub(C3D_RenderTarget* t) { return reinterpret_cast<Target*>(t); }
Target* targetFromColorBuf(const void* p);
void glTargetEnsure(Target& t);  // GL thread
void glTargetDestroy(Target& t); // GL thread
// GL thread: read a target back in DisplayTransfer layout (rows flipped).
// outFmt: GX_TRANSFER_FMT_* (RGBA8 -> A,B,G,R bytes; RGB8 -> B,G,R; RGB565 LE)
void glReadTarget(Target& t, void* out, int outFmt);

// Texture registry (texture.cpp)
TexRef texFind(const void* data);
TexRef texRegister(const C3D_Tex* tex);
void texForget(const void* data);
void texMarkDirty(const void* addr, size_t size);
void texUpload(TexMem& t); // GL thread
void texApplySampler(TexMem& t, u32 param);
unsigned texLutGL(const std::vector<float>* lut); // GL thread

// Display (gfx.cpp)
// LCD layout; `direct`: draw the screen's GPU snapshot (glSnapshotTarget) instead of `pixels`.
struct ScreenImage { const u8* pixels; int format; int width, height; bool direct; };
// GL thread: copy a finished target into screen's snapshot texture / get it.
void glSnapshotTarget(Target& t, int screen);
unsigned glSnapshotTex(int screen);
int glSnapshotHeight(int screen); // texture rows = LCD columns
void glPresent(const ScreenImage& top, const ScreenImage& bottom); // GL thread
}
