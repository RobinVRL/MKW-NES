// The GL thread: owns the EGL context (Mali fbdev window), executes the
// command lists recorded by c3d.cpp in order, and keeps every GL object.
#include "backend.hpp"
#include "gl.hpp"
#include "shaders.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <linux/fb.h>
#include <map>
#include <mutex>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

using namespace gl;

namespace c3d {
namespace {
std::mutex qLock;
std::condition_variable qCv, idleCv;
std::deque<std::vector<Cmd>> queue;
bool executing = false;
std::once_flag started;
std::thread thread;
std::vector<unsigned> garbage;
std::mutex garbageLock;
float lastMs = 0;

bool ready = false;
EGLDisplay dpy = EGL_NO_DISPLAY;
EGLSurface surf = EGL_NO_SURFACE;
EGLContext ctx = EGL_NO_CONTEXT;
struct FbdevWindow { unsigned short width, height; } fbWin; // Mali fbdev native window
int winW = 1280, winH = 720;

// ---------------------------------------------------------------- EGL
void fbSize() {
    if (const char* w = getenv("MKW_FB_WIDTH")) winW = atoi(w);
    if (const char* h = getenv("MKW_FB_HEIGHT")) winH = atoi(h);
    if (getenv("MKW_FB_WIDTH")) return;
    int fd = open("/dev/fb0", O_RDONLY);
    if (fd < 0) return;
    fb_var_screeninfo v{};
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0 && v.xres && v.yres) { winW = int(v.xres); winH = int(v.yres); }
    close(fd);
}

bool initEgl() {
    if (!gl::load()) return false;
    fbSize();
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) {
        fprintf(stderr, "[gl] eglInitialize failed 0x%x\n", eglGetError());
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    // MKW_HEADLESS: off-screen pbuffer (PC testing with Mesa; frames via MKW_DUMP)
    const bool headless = getenv("MKW_HEADLESS") != nullptr;
    if (headless) { winW = 1280; winH = 720; }
    const EGLint attrs[] = {EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_DEPTH_SIZE, 0,
                            EGL_SURFACE_TYPE, headless ? EGL_PBUFFER_BIT : EGL_WINDOW_BIT,
                            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n) || n < 1) {
        fprintf(stderr, "[gl] no EGL config 0x%x\n", eglGetError());
        return false;
    }
    fbWin.width = (unsigned short)winW;
    fbWin.height = (unsigned short)winH;
    if (headless) {
        const EGLint pb[] = {EGL_WIDTH, winW, EGL_HEIGHT, winH, EGL_NONE};
        surf = eglCreatePbufferSurface(dpy, cfg, pb);
    } else
        surf = eglCreateWindowSurface(dpy, cfg, reinterpret_cast<EGLNativeWindowType>(&fbWin), nullptr);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "[gl] eglCreateWindowSurface failed 0x%x\n", eglGetError());
        return false;
    }
    const EGLint cattrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattrs);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, surf, surf, ctx)) {
        fprintf(stderr, "[gl] context failed 0x%x\n", eglGetError());
        return false;
    }
    eglSwapInterval(dpy, getenv("MKW_NO_VSYNC") ? 0 : 1);
    EGLint w = 0, h = 0;
    if (eglQuerySurface(dpy, surf, EGL_WIDTH, &w) && w > 0) winW = w;
    if (eglQuerySurface(dpy, surf, EGL_HEIGHT, &h) && h > 0) winH = h;
    readExtensions();
    fprintf(stderr, "[gl] window %dx%d\n", winW, winH);
    return true;
}

// ---------------------------------------------------------------- programs
unsigned compile(GLenum type, const char* src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "[gl] %s shader failed:\n%s\n--- source ---\n%s\n", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log, src);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

struct Prog {
    unsigned id = 0;
    int c = -1, ub = -1, depth = -1, flip = -1, konst = -1, bufColor = -1, alphaRef = -1;
    int ambient = -1, lpos = -1, ldiff = -1, lspec = -1;
    const std::vector<float>* lastUnif = nullptr;
};
std::map<FragKey, Prog> programs;
unsigned vertexShaders[ProgCount] = {};

Prog* programFor(const FragKey& k) {
    auto it = programs.find(k);
    if (it != programs.end()) return it->second.id ? &it->second : nullptr;
    Prog& p = programs[k];
    if (!vertexShaders[k.program]) vertexShaders[k.program] = compile(GL_VERTEX_SHADER, vertexSource(k.program).c_str());
    const std::string fs = fragmentSource(k);
    unsigned f = compile(GL_FRAGMENT_SHADER, fs.c_str());
    if (!vertexShaders[k.program] || !f) return nullptr;
    unsigned id = glCreateProgram();
    glAttachShader(id, vertexShaders[k.program]);
    glAttachShader(id, f);
    static const char* names[6] = {"a0", "a1", "a2", "a3", "a4", "a5"};
    for (int i = 0; i < 6; ++i) glBindAttribLocation(id, GLuint(i), names[i]);
    glLinkProgram(id);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(id, sizeof log, nullptr, log);
        fprintf(stderr, "[gl] link failed: %s\n", log);
        glDeleteProgram(id);
        return nullptr;
    }
    p.id = id;
    glUseProgram(id);
    glUniform1i(glGetUniformLocation(id, "t0"), 0);
    glUniform1i(glGetUniformLocation(id, "t1"), 1);
    glUniform1i(glGetUniformLocation(id, "t2"), 2);
    glUniform1i(glGetUniformLocation(id, "tLut"), 3);
    p.c = glGetUniformLocation(id, "c");
    if (p.c < 0) p.c = glGetUniformLocation(id, "c[0]");
    p.ub = glGetUniformLocation(id, "ub");
    if (p.ub < 0) p.ub = glGetUniformLocation(id, "ub[0]");
    p.depth = glGetUniformLocation(id, "uDepth");
    p.flip = glGetUniformLocation(id, "uFlip");
    p.konst = glGetUniformLocation(id, "uConst");
    p.bufColor = glGetUniformLocation(id, "uBufColor");
    p.alphaRef = glGetUniformLocation(id, "uAlphaRef");
    p.ambient = glGetUniformLocation(id, "uAmbient");
    p.lpos = glGetUniformLocation(id, "uLPos");
    p.ldiff = glGetUniformLocation(id, "uLDiff");
    p.lspec = glGetUniformLocation(id, "uLSpec");
    return &p;
}

// ---------------------------------------------------------------- state translation
GLenum testFunc(int f) {
    static const GLenum t[8] = {GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL};
    return t[f & 7];
}
GLenum stencilOp(int o) {
    static const GLenum t[8] = {GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
    return t[o & 7];
}
GLenum blendFactor(int f) {
    static const GLenum t[15] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR,
                                 GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA,
                                 GL_ONE_MINUS_DST_ALPHA, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR,
                                 GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_SRC_ALPHA_SATURATE};
    return f >= 0 && f < 15 ? t[f] : GL_ONE;
}
GLenum blendEq(int e) {
    switch (e) {
    case GPU_BLEND_SUBTRACT: return GL_FUNC_SUBTRACT;
    case GPU_BLEND_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
    case GPU_BLEND_MIN: return hasMinMax ? GL_MIN_EXT : GL_FUNC_ADD;
    case GPU_BLEND_MAX: return hasMinMax ? GL_MAX_EXT : GL_FUNC_ADD;
    default: return GL_FUNC_ADD;
    }
}
void rgba(u32 c, float o[4]) { // 0xAABBGGRR
    o[0] = float(c & 0xFF) / 255.f; o[1] = float(c >> 8 & 0xFF) / 255.f;
    o[2] = float(c >> 16 & 0xFF) / 255.f; o[3] = float(c >> 24) / 255.f;
}

Target* boundTarget = nullptr;
void bindTarget(Target* t) {
    if (boundTarget == t) return;
    glTargetEnsure(*t);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    boundTarget = t;
}

void runClear(const Cmd& c) {
    bindTarget(c.target);
    glDisable(GL_SCISSOR_TEST);
    GLbitfield bits = 0;
    if (c.clearBits & C3D_CLEAR_COLOR) {
        const u32 v = c.clearColor; // 0xRRGGBBAA
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(float(v >> 24) / 255.f, float(v >> 16 & 0xFF) / 255.f, float(v >> 8 & 0xFF) / 255.f, float(v & 0xFF) / 255.f);
        bits |= GL_COLOR_BUFFER_BIT;
    }
    if ((c.clearBits & C3D_CLEAR_DEPTH) && c.target->depthFmt >= 0) {
        glDepthMask(GL_TRUE);
        const bool d16 = c.target->depthFmt == GPU_RB_DEPTH16;
        glClearDepthf(d16 ? float(c.clearDepth & 0xFFFF) / 65535.f : float(c.clearDepth & 0xFFFFFF) / 16777215.f);
        bits |= GL_DEPTH_BUFFER_BIT;
        if (c.target->depthFmt == GPU_RB_DEPTH24_STENCIL8) {
            glStencilMask(0xFF);
            glClearStencil(GLint(c.clearDepth >> 24));
            bits |= GL_STENCIL_BUFFER_BIT;
        }
    }
    if (bits) glClear(bits);
}

int enabledAttribs = 0;

void runDraw(const Cmd& c) {
    const DrawState& s = *c.state;
    if (s.program < 0 || s.program >= ProgCount) return;
    bindTarget(c.target);

    FragKey k;
    memset(&k, 0, sizeof k);
    for (int i = 0; i < 6; ++i) {
        const C3D_TexEnv& e = s.env[i];
        k.stage[i] = {e.srcRgb, e.srcAlpha, u16(e.opRgb), u16(e.opAlpha), u8(e.funcRgb), u8(e.funcAlpha), u8(e.scaleRgb), u8(e.scaleAlpha)};
    }
    k.bufUpdate = s.bufUpdate & 0xFF00;
    k.program = u8(s.program);
    k.alphaTest = s.alphaTest;
    k.alphaFunc = u8(s.alphaFunc);
    k.projective = ((c.texParam[0] >> 28) & 7) == GPU_TEX_PROJECTION;
    if (s.lighting.enabled && programLit(s.program)) {
        k.lights = u8(s.lighting.count);
        k.d0 = s.lighting.d0 != nullptr;
        k.d0Abs = s.lighting.d0Abs;
    }
    Prog* p = programFor(k);
    if (!p) return;
    glUseProgram(p->id);

    // uniforms
    const int nvec = uniformVectors(s.program);
    if (p->c >= 0 && p->lastUnif != c.unif.get()) {
        glUniform4fv(p->c, nvec, c.unif->data());
        p->lastUnif = c.unif.get();
    }
    if (p->ub >= 0) {
        GLint b[16];
        const int nb = boolCount(s.program);
        for (int i = 0; i < nb; ++i) b[i] = (c.bools >> i) & 1;
        glUniform1iv(p->ub, nb, b);
    }
    glUniform2f(p->depth, s.zScale, s.zOffset);
    {
        float flip[3];
        for (int i = 0; i < 3; ++i) flip[i] = (c.tex[i] && !c.tex[i]->renderTarget) ? 1.f : 0.f;
        glUniform3fv(p->flip, 1, flip);
        float k6[24];
        for (int i = 0; i < 6; ++i) rgba(s.env[i].color, k6 + 4 * i);
        glUniform4fv(p->konst, 6, k6);
        float bc[4];
        rgba(s.bufColor, bc);
        glUniform4fv(p->bufColor, 1, bc);
        glUniform1f(p->alphaRef, float(s.alphaRef));
    }
    if (k.lights) {
        const LightingSnap& L = s.lighting;
        glUniform3fv(p->ambient, 1, L.ambient);
        float pos[32], diff[24], spec[24];
        for (int i = 0; i < L.count; ++i) {
            memcpy(pos + 4 * i, L.light[i].pos, 16);
            memcpy(diff + 3 * i, L.light[i].diffuse, 12);
            memcpy(spec + 3 * i, L.light[i].spec0, 12);
        }
        glUniform4fv(p->lpos, L.count, pos);
        glUniform3fv(p->ldiff, L.count, diff);
        glUniform3fv(p->lspec, L.count, spec);
        if (k.d0) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, texLutGL(L.d0.get()));
        }
    }

    // textures
    const u8 used = k.textureMask();
    for (int i = 0; i < 3; ++i) {
        if (!(used >> i & 1)) continue;
        glActiveTexture(GL_TEXTURE0 + GLenum(i));
        TexMem* t = c.tex[i].get();
        if (!t) { glBindTexture(GL_TEXTURE_2D, 0); continue; }
        if (!t->renderTarget && t->uploaded != t->generation.load()) texUpload(*t);
        glBindTexture(GL_TEXTURE_2D, t->glTex);
        texApplySampler(*t, c.texParam[i]);
    }

    // fixed function
    glViewport(s.vp[0], s.vp[1], s.vp[2], s.vp[3]);
    if (s.scissorMode == GPU_SCISSOR_NORMAL) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(s.scissor[0], s.scissor[1], s.scissor[2] - s.scissor[0], s.scissor[3] - s.scissor[1]);
    } else glDisable(GL_SCISSOR_TEST);
    if (s.cull == GPU_CULL_NONE) glDisable(GL_CULL_FACE);
    else { glEnable(GL_CULL_FACE); glFrontFace(GL_CCW); glCullFace(s.cull == GPU_CULL_FRONT_CCW ? GL_FRONT : GL_BACK); }
    const bool hasDepth = c.target->depthFmt >= 0;
    const bool depthWrite = (s.writeMask & GPU_WRITE_DEPTH) != 0;
    if (hasDepth && (s.depthTest || depthWrite)) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(s.depthTest ? testFunc(s.depthFunc) : GL_ALWAYS);
        glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    } else glDisable(GL_DEPTH_TEST);
    glColorMask(s.writeMask & 1 ? GL_TRUE : GL_FALSE, s.writeMask & 2 ? GL_TRUE : GL_FALSE,
                s.writeMask & 4 ? GL_TRUE : GL_FALSE, s.writeMask & 8 ? GL_TRUE : GL_FALSE);
    if (s.stencil && c.target->depthFmt == GPU_RB_DEPTH24_STENCIL8) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(testFunc(s.stencilFunc), s.stencilRef, GLuint(s.stencilIn));
        glStencilMask(GLuint(s.stencilWrite));
        glStencilOp(stencilOp(s.sfail), stencilOp(s.dfail), stencilOp(s.spass));
    } else glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(blendEq(s.blendEq[0]), blendEq(s.blendEq[1]));
    glBlendFuncSeparate(blendFactor(s.blend[0]), blendFactor(s.blend[1]), blendFactor(s.blend[2]), blendFactor(s.blend[3]));
    { float bc[4]; rgba(s.blendColor, bc); glBlendColor(bc[0], bc[1], bc[2], bc[3]); }

    // vertex input
    int mask = 0;
    for (int b = 0; b < s.nbuf; ++b) {
        const DrawState::Buf& buf = s.buf[b];
        for (int a = 0; a < buf.nattr; ++a) {
            const DrawState::Attr& at = buf.attr[a];
            if (at.reg >= 6) continue;
            static const GLenum types[4] = {GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_FLOAT};
            glVertexAttribPointer(GLuint(at.reg), at.count, types[at.format & 3], GL_FALSE, buf.stride, buf.base + at.offset);
            mask |= 1 << at.reg;
        }
    }
    for (int r = 0; r < 6; ++r) {
        const bool want = mask >> r & 1, have = enabledAttribs >> r & 1;
        if (want && !have) glEnableVertexAttribArray(GLuint(r));
        if (!want && have) glDisableVertexAttribArray(GLuint(r));
        if (!want) glVertexAttrib4f(GLuint(r), 0, 0, 0, 1);
    }
    enabledAttribs = mask;

    GLenum prim = GL_TRIANGLES;
    if (c.prim == GPU_TRIANGLE_STRIP) prim = GL_TRIANGLE_STRIP;
    else if (c.prim == GPU_TRIANGLE_FAN) prim = GL_TRIANGLE_FAN;
    if (c.indexType < 0) glDrawArrays(prim, c.first, c.count);
    else glDrawElements(prim, c.count, c.indexType == C3D_UNSIGNED_SHORT ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, c.indices);
}

void collectGarbage() {
    std::vector<unsigned> g;
    { std::lock_guard<std::mutex> l(garbageLock); g.swap(garbage); }
    if (!g.empty() && ready) glDeleteTextures(GLsizei(g.size()), g.data());
}

void loop() {
    ready = initEgl();
    if (!ready) fprintf(stderr, "[gl] running without a display: commands are discarded\n");
    for (;;) {
        std::vector<Cmd> l;
        {
            std::unique_lock<std::mutex> g(qLock);
            qCv.wait(g, [] { return !queue.empty(); });
            l = std::move(queue.front());
            queue.pop_front();
            executing = true;
        }
        const auto t0 = std::chrono::steady_clock::now();
        bool drew = false;
        for (Cmd& c : l) {
            switch (c.kind) {
            case Cmd::Draw: if (ready) { runDraw(c); drew = true; } break;
            case Cmd::Clear: if (ready) runClear(c); break;
            case Cmd::Call: if (c.fn) c.fn(); break;
            }
        }
        collectGarbage();
        if (drew) lastMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        l.clear(); // drop references before reporting idle
        {
            std::lock_guard<std::mutex> g(qLock);
            executing = false;
        }
        idleCv.notify_all();
    }
}
}

// ---------------------------------------------------------------- interface
void glStart() {
    std::call_once(started, [] {
        thread = std::thread(loop);
        thread.detach();
    });
}
void glSubmit(std::vector<Cmd>&& list) {
    glStart();
    {
        std::lock_guard<std::mutex> g(qLock);
        queue.push_back(std::move(list));
    }
    qCv.notify_one();
}
void glWaitIdle() {
    std::unique_lock<std::mutex> g(qLock);
    idleCv.wait(g, [] { return queue.empty() && !executing; });
}
void glRun(std::function<void()> fn) {
    std::vector<Cmd> l(1);
    l[0].kind = Cmd::Call;
    l[0].fn = std::move(fn);
    glSubmit(std::move(l));
    glWaitIdle();
}
float glLastFrameMs() { return lastMs; }
void glGarbage(unsigned tex) {
    std::lock_guard<std::mutex> g(garbageLock);
    garbage.push_back(tex);
}
bool glReady() { return ready; }
void glWindowSize(int& w, int& h) { w = winW; h = winH; }
// MKW_DUMP=<dir>: write presented frames as PPM (every MKW_DUMP_EVERY-th, default 30).
// MKW_FRAMES=<n>: exit after n presented frames.
static void dumpFrame() {
    static unsigned frame = 0;
    ++frame;
    const char* dir = getenv("MKW_DUMP");
    static const unsigned every = getenv("MKW_DUMP_EVERY") ? unsigned(atoi(getenv("MKW_DUMP_EVERY"))) : 30;
    if (dir && every && frame % every == 0) {
        std::vector<u8> px(size_t(winW) * winH * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, winW, winH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        char path[512];
        snprintf(path, sizeof path, "%s/frame%05u.ppm", dir, frame);
        if (FILE* f = fopen(path, "wb")) {
            fprintf(f, "P6\n%d %d\n255\n", winW, winH);
            for (int y = winH - 1; y >= 0; --y)
                for (int x = 0; x < winW; ++x) fwrite(&px[(size_t(y) * winW + x) * 4], 1, 3, f);
            fclose(f);
        }
    }
    if (const char* n = getenv("MKW_FRAMES"))
        if (frame >= unsigned(atoi(n))) { fprintf(stderr, "[gl] %u frames presented, exiting\n", frame); fflush(nullptr); _exit(0); }
}
void glSwapWindow() {
    if (ready) dumpFrame();
    if (ready) eglSwapBuffers(dpy, surf);
    boundTarget = nullptr;
    enabledAttribs = 0; // glPresent leaves every attribute array disabled
}
unsigned glCompileProgram(const char* vs, const char* fs, const char* const* attribs) {
    unsigned v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    for (int i = 0; attribs && attribs[i]; ++i) glBindAttribLocation(p, GLuint(i), attribs[i]);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(p); return 0; }
    return p;
}

// ---------------------------------------------------------------- targets
void glTargetEnsure(Target& t) {
    if (t.fbo || !ready) return;
    TexMem* color = t.color.get();
    if (color && !color->glTex) {
        glGenTextures(1, &color->glTex);
        glBindTexture(GL_TEXTURE_2D, color->glTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        color->glLevels = 1;
        color->sampler = ~0u;
    } else if (color && color->glLevels > 1) {
        // A mipmapped texture becomes a target: keep level 0 only.
        glBindTexture(GL_TEXTURE_2D, color->glTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        color->glLevels = 1;
        color->sampler = ~0u;
    }
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    if (color) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->glTex, 0);
    if (t.depthFmt >= 0) {
        const bool stencil = t.depthFmt == GPU_RB_DEPTH24_STENCIL8;
        if (stencil && hasPackedDS) {
            glGenRenderbuffers(1, &t.depthRb);
            glBindRenderbuffer(GL_RENDERBUFFER, t.depthRb);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, t.width, t.height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depthRb);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.depthRb);
        } else {
            glGenRenderbuffers(1, &t.depthRb);
            glBindRenderbuffer(GL_RENDERBUFFER, t.depthRb);
            const bool d24 = t.depthFmt != GPU_RB_DEPTH16 && hasDepth24;
            glRenderbufferStorage(GL_RENDERBUFFER, d24 ? GL_DEPTH_COMPONENT24_OES : GL_DEPTH_COMPONENT16, t.width, t.height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depthRb);
            if (stencil) {
                glGenRenderbuffers(1, &t.stencilRb);
                glBindRenderbuffer(GL_RENDERBUFFER, t.stencilRb);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, t.width, t.height);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.stencilRb);
            }
        }
    }
    const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "[gl] target %dx%d incomplete 0x%x\n", t.width, t.height, st);
    boundTarget = nullptr;
}
void glTargetDestroy(Target& t) {
    if (!ready) return;
    if (boundTarget == &t) { glBindFramebuffer(GL_FRAMEBUFFER, 0); boundTarget = nullptr; }
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.depthRb) glDeleteRenderbuffers(1, &t.depthRb);
    if (t.stencilRb) glDeleteRenderbuffers(1, &t.stencilRb);
    t.fbo = t.depthRb = t.stencilRb = 0;
}
// Per-screen copies of the last finished frame, made on the GPU at
// C3D_FrameEnd. Presenting these instead of the live render target means
// the game clearing or redrawing (or reusing) the target before the TV
// output runs can't show up as flicker.
// The copy is a textured draw into an FBO on the snapshot: Mali-400's
// glCopyTexSubImage2D is very slow and halved the frame rate.
namespace {
unsigned snapTex[2] = {}, snapFbo[2] = {}, snapProg = 0;
int snapW[2] = {}, snapH[2] = {};
const char* kSnapVS = "attribute vec2 aPos; varying vec2 vUv;\n"
    "void main() { vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
const char* kSnapFS = "precision mediump float; varying vec2 vUv; uniform sampler2D tex;\n"
    "void main() { gl_FragColor = texture2D(tex, vUv); }\n";
}
int glSnapshotHeight(int screen) { return screen >= 0 && screen < 2 ? snapH[screen] : 0; }
void glSnapshotTarget(Target& t, int screen) {
    if (!ready || screen < 0 || screen > 1) return;
    glTargetEnsure(t);
    if (!t.fbo || !t.color || !t.color->glTex) return;
    if (!snapProg) {
        static const char* const attribs[] = {"aPos", nullptr};
        snapProg = glCompileProgram(kSnapVS, kSnapFS, attribs);
        if (!snapProg) return;
    }
    if (!snapTex[screen]) glGenTextures(1, &snapTex[screen]);
    glBindTexture(GL_TEXTURE_2D, snapTex[screen]);
    if (snapW[screen] != t.width || snapH[screen] != t.height) {
        fprintf(stderr, "[gl] screen %d target %dx%d\n", screen, t.width, t.height);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        snapW[screen] = t.width; snapH[screen] = t.height;
        if (!snapFbo[screen]) glGenFramebuffers(1, &snapFbo[screen]);
        glBindFramebuffer(GL_FRAMEBUFFER, snapFbo[screen]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, snapTex[screen], 0);
    }
    static const bool copy = getenv("MKW_SNAPCOPY") != nullptr;
    if (copy) {
        bindTarget(&t);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, t.width, t.height);
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, snapFbo[screen]);
    boundTarget = nullptr;
    glViewport(0, 0, t.width, t.height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(snapProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.color->glTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    t.color->sampler = ~0u;
    for (int i = 0; i < 8; ++i) glDisableVertexAttribArray(GLuint(i));
    static const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, quad);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    enabledAttribs = 0; // draws re-enable what they use
    glBindTexture(GL_TEXTURE_2D, 0);
    // Finish the frame on the GPU before the game builds the next one.
    // Without this the race scene (drawn to an offscreen target and composited
    // with bloom) came out blank blue: the game reuses memory the queued GL
    // work still reads. MKW_NOSNAPFINISH=1 skips it (for testing).
    static const bool noFinish = getenv("MKW_NOSNAPFINISH") != nullptr;
    if (!noFinish) glFinish();
}
unsigned glSnapshotTex(int screen) { return screen >= 0 && screen < 2 ? snapTex[screen] : 0; }

void glReadTarget(Target& t, void* out, int outFmt) {
    const int w = t.width, h = t.height;
    static std::vector<u8> px;
    px.assign(size_t(w) * h * 4, 0);
    if (ready) {
        static const bool log = getenv("MKW_TEXLOG") != nullptr;
        timespec a{}, b{};
        if (log) { clock_gettime(CLOCK_MONOTONIC, &a); fprintf(stderr, "[gl] readback %dx%d fmt %d ...", w, h, outFmt); }
        bindTarget(&t);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        if (log) { clock_gettime(CLOCK_MONOTONIC, &b); fprintf(stderr, " %.3fs\n", (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) * 1e-9); }
    }
    u8* o = static_cast<u8*>(out);
    for (int j = 0; j < h; ++j) {
        const u8* row = &px[size_t(h - 1 - j) * w * 4];
        for (int i = 0; i < w; ++i) {
            const u8* p = row + i * 4;
            switch (outFmt) {
            case GX_TRANSFER_FMT_RGBA8: o[0] = p[3]; o[1] = p[2]; o[2] = p[1]; o[3] = p[0]; o += 4; break;
            case GX_TRANSFER_FMT_RGB8: o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o += 3; break;
            case GX_TRANSFER_FMT_RGB565: {
                const u16 v = u16((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | (p[2] >> 3));
                memcpy(o, &v, 2); o += 2; break;
            }
            case GX_TRANSFER_FMT_RGB5A1: {
                const u16 v = u16((p[0] >> 3) << 11 | (p[1] >> 3) << 6 | (p[2] >> 3) << 1 | (p[3] >> 7));
                memcpy(o, &v, 2); o += 2; break;
            }
            default: {
                const u16 v = u16((p[0] >> 4) << 12 | (p[1] >> 4) << 8 | (p[2] >> 4) << 4 | (p[3] >> 4));
                memcpy(o, &v, 2); o += 2; break;
            }
            }
        }
    }
}
}
