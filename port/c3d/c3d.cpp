// The citro3d API on top of the GL thread. Calls record commands with
// snapshots of the state they depend on, exactly where citro3d would emit
// GPU commands; FrameSplit/FrameEnd hand the list to the GL thread, and
// FrameBegin waits for the GPU (GL thread) to drain like C3D_FrameBegin.
#include "backend.hpp"
#include "port_gfx.hpp"
#include <cstddef>
#include <cstring>
#include <mutex>

extern "C" {
C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
C3D_IVec C3D_IVUnif[2][C3D_IVUNIF_COUNT];
u16 C3D_BoolUnifs[2];
bool C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT];
bool C3D_IVUnifDirty[2][C3D_IVUNIF_COUNT];
bool C3D_BoolUnifsDirty[2];
}

void* vramAlloc(size_t size);
void vramFree(void* mem);
size_t vramGetSize(void* mem);

namespace c3d {
namespace {
std::recursive_mutex apiLock;
bool active = false;
bool inFrame = false;
std::vector<Cmd> list;
size_t listPeak = 1;

DrawState cur;
bool stateDirty = true;
std::shared_ptr<const DrawState> stateSnap;
std::shared_ptr<const std::vector<float>> unifSnap;

C3D_AttrInfo attrInfo;
C3D_BufInfo bufInfo;
C3D_Tex* bound[3] = {};
C3D_LightEnv* lightEnv = nullptr;
int program = -1;

Target* current = nullptr;
Target* links[2][2] = {};
std::vector<Target*> targets;

void freeMem(void* p) {
    if (!p) return;
    if (vramGetSize(p)) vramFree(p);
    else linearFree(p);
}

u32 componentSize(int fmt) { return fmt == GPU_FLOAT ? 4 : fmt == GPU_SHORT ? 2 : 1; }

void decodeVertexInput(DrawState& s) {
    s.nbuf = 0;
    for (int b = 0; b < bufInfo.bufCount && b < 12; ++b) {
        const C3D_BufCfg& cfg = bufInfo.buffers[b];
        DrawState::Buf& out = s.buf[s.nbuf];
        out.base = reinterpret_cast<const u8*>(uintptr_t(bufInfo.base_paddr + cfg.offset));
        out.stride = int(cfg.flags[1] >> 16 & 0xFFF);
        const int n = int(cfg.flags[1] >> 28);
        const u64 perm = u64(cfg.flags[0]) | u64(cfg.flags[1] & 0xFFFF) << 32;
        out.nattr = 0;
        u32 off = 0;
        for (int j = 0; j < n; ++j) {
            const int id = int(perm >> (4 * j) & 0xF);
            if (id >= 0xC) { off += 4 * (id - 0xB); continue; }
            if (id >= attrInfo.attrCount) continue;
            const u32 nib = (id < 8 ? attrInfo.flags[0] >> (4 * id) : attrInfo.flags[1] >> (4 * (id - 8))) & 0xF;
            const int fmt = int(nib & 3), count = int(nib >> 2) + 1;
            const u32 cs = componentSize(fmt);
            off = (off + cs - 1) / cs * cs;
            DrawState::Attr& a = out.attr[out.nattr++];
            a.reg = int(attrInfo.permutation >> (4 * id) & 0xF);
            a.format = fmt; a.count = count; a.offset = off;
            off += cs * u32(count);
        }
        ++s.nbuf;
    }
}

float lutValue(const C3D_LightLut* lut, int i) { return float(lut->data[i] & 0xFFF) / 4096.f; }

void snapshotLighting(LightingSnap& out) {
    out = LightingSnap{};
    C3D_LightEnv* env = lightEnv;
    if (!env) return;
    out.enabled = true;
    const C3D_Material& m = env->material;
    for (int c = 0; c < 3; ++c) out.ambient[c] = env->ambient[c] * m.ambient[c] + m.emission[c];
    for (int i = 0; i < 8; ++i) {
        C3D_Light* l = env->lights[i];
        if (!l || !(l->flags & C3DF_Light_Enabled)) continue;
        LightSnap& o = out.light[out.count++];
        memcpy(o.pos, l->conf.position, sizeof o.pos);
        for (int c = 0; c < 3; ++c) {
            out.ambient[c] += l->ambient[c] * m.ambient[c];
            o.diffuse[c] = l->diffuse[c] * m.diffuse[c];
            o.spec0[c] = l->specular0[c] * m.specular0[c];
        }
    }
    if (const C3D_LightLut* lut = env->luts[GPU_LUT_D0]) {
        auto v = std::make_shared<std::vector<float>>(256);
        out.d0Abs = env->conf.lutInput.abs >> GPU_LUT_D0 & 1;
        // Signed LUTs keep [0,1] in entries 0..127; resample to the abs layout.
        for (int i = 0; i < 256; ++i) (*v)[i] = lutValue(lut, out.d0Abs ? i : (i >> 1));
        out.d0 = v;
    }
}

std::shared_ptr<const DrawState> snapshotState() {
    if (stateDirty || !stateSnap) {
        auto s = std::make_shared<DrawState>(cur);
        s->program = program;
        decodeVertexInput(*s);
        snapshotLighting(s->lighting);
        stateSnap = s;
        stateDirty = false;
    }
    return stateSnap;
}

std::shared_ptr<const std::vector<float>> snapshotUniforms() {
    bool dirty = !unifSnap;
    for (int i = 0; i < C3D_FVUNIF_COUNT && !dirty; ++i) dirty = C3D_FVUnifDirty[0][i];
    if (dirty) {
        auto v = std::make_shared<std::vector<float>>(C3D_FVUNIF_COUNT * 4);
        for (int i = 0; i < C3D_FVUNIF_COUNT; ++i) {
            const C3D_FVec& f = C3D_FVUnif[0][i];
            float* o = &(*v)[i * 4];
            o[0] = f.x; o[1] = f.y; o[2] = f.z; o[3] = f.w;
            C3D_FVUnifDirty[0][i] = false;
        }
        unifSnap = v;
    }
    return unifSnap;
}

void submit() {
    if (list.empty()) return;
    listPeak = list.size() > listPeak ? list.size() : listPeak;
    glSubmit(std::move(list));
    list.clear();
}

void recordDraw(int prim, int first, int count, int indexType, const void* indices) {
    if (!current || program < 0 || count <= 0) return;
    Cmd c{Cmd::Draw};
    c.state = snapshotState();
    c.unif = snapshotUniforms();
    c.bools = C3D_BoolUnifs[0];
    c.prim = prim; c.first = first; c.count = count; c.indexType = indexType; c.indices = indices;
    c.target = current;
    for (int i = 0; i < 3; ++i)
        if (C3D_Tex* t = bound[i]) {
            TexRef r = texFind(t->data);
            if (!r) r = texRegister(t);
            c.tex[i] = r;
            c.texParam[i] = t->param;
        }
    list.push_back(std::move(c));
}

void defaults() {
    cur = DrawState{};
    for (auto& e : cur.env) C3D_TexEnvInit(&e);
    cur.zBuffer = true; cur.zScale = -1.f; cur.zOffset = 0.f;
    cur.cull = GPU_CULL_BACK_CCW;
    cur.depthTest = true; cur.depthFunc = GPU_GREATER; cur.writeMask = GPU_WRITE_ALL;
    AttrInfo_Init(&attrInfo);
    BufInfo_Init(&bufInfo);
    memset(bound, 0, sizeof bound);
    lightEnv = nullptr;
    program = -1;
    stateDirty = true;
}
}

Target* targetFromColorBuf(const void* p) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    for (Target* t : targets)
        if (t->pub.frameBuf.colorBuf == p) return t;
    return nullptr;
}

// Called by gfx.cpp when it needs every queued command executed first.
void flushRecorded() {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    submit();
}
}

using namespace c3d;

// ------------------------------------------------------------------ base
bool C3D_Init(size_t) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (active) return true;
    glStart();
    defaults();
    active = true;
    return true;
}
void C3D_Fini(void) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (!active) return;
    submit();
    glWaitIdle();
    active = false;
}
float C3D_GetCmdBufUsage(void) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    return float(list.size()) / 8192.f;
}
void C3D_BindProgram(shaderProgram_s* p) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    program = (p && p->vertexShader && p->vertexShader->dvle) ? int(p->vertexShader->dvle->mainOffset) : -1;
    stateDirty = true;
}
void C3D_SetViewport(u32 x, u32 y, u32 w, u32 h) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.vp[0] = int(x); cur.vp[1] = int(y); cur.vp[2] = int(w); cur.vp[3] = int(h);
    stateDirty = true;
    static const bool log = getenv("MKW_VPLOG") != nullptr;
    if (log) fprintf(stderr, "[vp] set %u,%u,%u,%u\n", x, y, w, h);
}
void C3D_SetScissor(GPU_SCISSORMODE mode, u32 left, u32 top, u32 right, u32 bottom) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.scissorMode = mode;
    cur.scissor[0] = int(left); cur.scissor[1] = int(top); cur.scissor[2] = int(right); cur.scissor[3] = int(bottom);
    stateDirty = true;
}
void C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    recordDraw(primitive, first, size, -1, nullptr);
}
void C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void* indices) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    recordDraw(primitive, 0, count, type, indices);
}

// ------------------------------------------------------------------ attributes / buffers
void AttrInfo_Init(C3D_AttrInfo* info) {
    memset(info, 0, sizeof(*info));
    info->flags[1] = 0xFFF << 16;
}
int AttrInfo_AddLoader(C3D_AttrInfo* info, int regId, GPU_FORMATS format, int count) {
    if (info->attrCount == 12) return -1;
    int id = info->attrCount++;
    if (regId < 0) regId = id;
    if (id < 8) info->flags[0] |= GPU_ATTRIBFMT(id, count, format);
    else info->flags[1] |= GPU_ATTRIBFMT(id - 8, count, format);
    info->flags[1] = (info->flags[1] & ~(0xF0000000 | BIT(id + 16))) | (id << 28);
    info->permutation |= u64(regId) << (id * 4);
    return id;
}
int AttrInfo_AddFixed(C3D_AttrInfo* info, int regId) {
    if (info->attrCount == 12) return -1;
    int id = info->attrCount++;
    if (regId < 0) regId = id;
    info->flags[1] = (info->flags[1] & ~0xF0000000) | (id << 28);
    info->permutation |= u64(regId) << (id * 4);
    return id;
}
C3D_AttrInfo* C3D_GetAttrInfo(void) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    stateDirty = true;
    return &attrInfo;
}
void C3D_SetAttrInfo(C3D_AttrInfo* info) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (info != &attrInfo) attrInfo = *info;
    stateDirty = true;
}
void BufInfo_Init(C3D_BufInfo* info) {
    memset(info, 0, sizeof(*info));
    info->base_paddr = 0; // offsets are virtual addresses on Linux
}
int BufInfo_Add(C3D_BufInfo* info, const void* data, ptrdiff_t stride, int attribCount, u64 permutation) {
    if (info->bufCount == 12) return -1;
    int id = info->bufCount++;
    C3D_BufCfg* buf = &info->buffers[id];
    buf->offset = u32(uintptr_t(data));
    buf->flags[0] = u32(permutation & 0xFFFFFFFF);
    buf->flags[1] = u32(permutation >> 32) | (u32(stride) << 16) | (u32(attribCount) << 28);
    return id;
}
C3D_BufInfo* C3D_GetBufInfo(void) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    stateDirty = true;
    return &bufInfo;
}
void C3D_SetBufInfo(C3D_BufInfo* info) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (info != &bufInfo) bufInfo = *info;
    stateDirty = true;
}

// ------------------------------------------------------------------ effect
void C3D_DepthMap(bool bIsZBuffer, float zScale, float zOffset) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.zBuffer = bIsZBuffer; cur.zScale = zScale; cur.zOffset = zOffset;
    stateDirty = true;
}
void C3D_CullFace(GPU_CULLMODE mode) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.cull = mode; stateDirty = true;
}
void C3D_StencilTest(bool enable, GPU_TESTFUNC function, int ref, int inputMask, int writeMask) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.stencil = enable; cur.stencilFunc = function; cur.stencilRef = ref & 0xFF;
    cur.stencilIn = inputMask & 0xFF; cur.stencilWrite = writeMask & 0xFF;
    stateDirty = true;
}
void C3D_StencilOp(GPU_STENCILOP sfail, GPU_STENCILOP dfail, GPU_STENCILOP pass) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.sfail = sfail; cur.dfail = dfail; cur.spass = pass; stateDirty = true;
}
void C3D_BlendingColor(u32 color) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.blendColor = color; stateDirty = true;
}
void C3D_EarlyDepthTest(bool, GPU_EARLYDEPTHFUNC, u32) {}
void C3D_DepthTest(bool enable, GPU_TESTFUNC function, GPU_WRITEMASK writemask) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.depthTest = enable; cur.depthFunc = function; cur.writeMask = writemask; stateDirty = true;
}
void C3D_AlphaTest(bool enable, GPU_TESTFUNC function, int ref) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.alphaTest = enable; cur.alphaFunc = function; cur.alphaRef = ref & 0xFF; stateDirty = true;
}
void C3D_AlphaBlend(GPU_BLENDEQUATION colorEq, GPU_BLENDEQUATION alphaEq, GPU_BLENDFACTOR srcClr, GPU_BLENDFACTOR dstClr,
                    GPU_BLENDFACTOR srcAlpha, GPU_BLENDFACTOR dstAlpha) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.blendEq[0] = colorEq; cur.blendEq[1] = alphaEq;
    cur.blend[0] = srcClr; cur.blend[1] = dstClr; cur.blend[2] = srcAlpha; cur.blend[3] = dstAlpha;
    stateDirty = true;
}
void C3D_ColorLogicOp(GPU_LOGICOP) {}
void C3D_FragOpMode(GPU_FRAGOPMODE) {}
void C3D_FragOpShadow(float, float) {}

// ------------------------------------------------------------------ texenv
C3D_TexEnv* C3D_GetTexEnv(int id) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    stateDirty = true;
    return &cur.env[id < 0 ? 0 : id > 5 ? 5 : id];
}
void C3D_SetTexEnv(int id, C3D_TexEnv* env) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (id < 0 || id > 5) return;
    if (env) cur.env[id] = *env; else C3D_TexEnvInit(&cur.env[id]);
    stateDirty = true;
}
void C3D_DirtyTexEnv(C3D_TexEnv*) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    stateDirty = true;
}
void C3D_TexEnvBufUpdate(int mode, int mask) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    u32 val = cur.bufUpdate;
    mask &= 0xF;
    if (mode & C3D_RGB) { val &= ~(0xFu << 8); val |= u32(mask) << 8; }
    if (mode & C3D_Alpha) { val &= ~(0xFu << 12); val |= u32(mask) << 12; }
    cur.bufUpdate = val; stateDirty = true;
}
void C3D_TexEnvBufColor(u32 color) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    cur.bufColor = color; stateDirty = true;
}

// ------------------------------------------------------------------ textures
static u32 texFmtBits(GPU_TEXCOLOR f) {
    switch (f) {
    case GPU_RGBA8: return 32;
    case GPU_RGB8: return 24;
    case GPU_RGBA5551: case GPU_RGB565: case GPU_RGBA4: case GPU_LA8: case GPU_HILO8: return 16;
    case GPU_L8: case GPU_A8: case GPU_LA4: case GPU_ETC1A4: return 8;
    case GPU_L4: case GPU_A4: case GPU_ETC1: return 4;
    default: return 0;
    }
}
static bool texSizeOk(u32 s) { return s >= 8 && s <= 1024 && !(s & (s - 1)); }

bool C3D_TexInitWithParams(C3D_Tex* tex, C3D_TexCube* cube, C3D_TexInitParams p) {
    if (!texSizeOk(p.width) || !texSizeOk(p.height)) return false;
    const bool isCube = p.type == GPU_TEX_CUBE_MAP || p.type == GPU_TEX_SHADOW_CUBE;
    if (isCube && !cube) return false;
    u32 size = texFmtBits(p.format);
    if (!size) return false;
    size *= u32(p.width) * p.height / 8;
    const u32 total = C3D_TexCalcTotalSize(size, p.maxLevel);
    if (!isCube) {
        tex->data = p.onVram ? vramAlloc(total) : linearAlloc(total);
        if (!tex->data) return false;
    } else {
        memset(cube, 0, sizeof(*cube));
        for (int i = 0; i < 6; ++i) {
            cube->data[i] = p.onVram ? vramAlloc(total) : linearAlloc(total);
            if (!cube->data[i]) {
                for (int j = 0; j < i; ++j) freeMem(cube->data[j]);
                return false;
            }
        }
        tex->cube = cube;
    }
    tex->width = p.width;
    tex->height = p.height;
    tex->param = GPU_TEXTURE_MODE(p.type);
    if (p.format == GPU_ETC1) tex->param |= GPU_TEXTURE_ETC1_PARAM;
    if (p.type == GPU_TEX_SHADOW_2D || p.type == GPU_TEX_SHADOW_CUBE) tex->param |= GPU_TEXTURE_SHADOW_PARAM;
    tex->fmt = p.format;
    tex->size = size;
    tex->border = 0;
    tex->lodBias = 0;
    tex->maxLevel = p.maxLevel;
    tex->minLevel = 0;
    texRegister(tex);
    return true;
}
void C3D_TexLoadImage(C3D_Tex* tex, const void* data, GPU_TEXFACE face, int level) {
    const bool is2D = ((tex->param >> 28) & 7) != GPU_TEX_CUBE_MAP && ((tex->param >> 28) & 7) != GPU_TEX_SHADOW_CUBE;
    u32 size = 0;
    void* out = C3D_TexGetImagePtr(tex, is2D ? tex->data : tex->cube->data[face], level, &size);
    memcpy(out, data, size);
    texMarkDirty(out, size);
}
void C3D_TexGenerateMipmap(C3D_Tex*, GPU_TEXFACE) {}
void C3D_TexBind(int unitId, C3D_Tex* tex) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (unitId < 0 || unitId >= 3) return;
    if (tex && unitId > 0 && C3D_TexGetType(tex) != GPU_TEX_2D) return;
    bound[unitId] = tex;
}
void C3D_TexFlush(C3D_Tex* tex) {
    if (tex && tex->data) texMarkDirty(tex->data, C3D_TexCalcTotalSize(tex->size, tex->maxLevel));
}
void C3D_TexDelete(C3D_Tex* tex) {
    const int type = (tex->param >> 28) & 7;
    if (type != GPU_TEX_CUBE_MAP && type != GPU_TEX_SHADOW_CUBE) {
        texForget(tex->data);
        freeMem(tex->data);
    } else if (tex->cube) {
        for (int i = 0; i < 6; ++i) {
            texForget(tex->cube->data[i]);
            freeMem(tex->cube->data[i]);
            tex->cube->data[i] = nullptr;
        }
    }
}
void C3D_TexShadowParams(bool, float) {}

extern "C" void portMarkDirty(const void* addr, size_t size) { texMarkDirty(addr, size); }

// ------------------------------------------------------------------ render targets
static Target* newTarget(int w, int h, C3D_DEPTHTYPE depthFmt) {
    Target* t = new Target{};
    t->width = w; t->height = h;
    int raw;
    static_assert(sizeof(C3D_DEPTHTYPE) == sizeof(int));
    memcpy(&raw, &depthFmt, sizeof raw);
    t->depthFmt = raw >= 0 ? raw : -1;
    C3D_FrameBuf& fb = t->pub.frameBuf;
    C3D_FrameBufAttrib(&fb, u16(w), u16(h), false);
    if (t->depthFmt >= 0) {
        const u32 bytes = u32(w) * h * (t->depthFmt == GPU_RB_DEPTH16 ? 2 : t->depthFmt == GPU_RB_DEPTH24 ? 3 : 4);
        void* d = vramAlloc(bytes);
        C3D_FrameBufDepth(&fb, d, GPU_DEPTHBUF(t->depthFmt));
        t->pub.ownsDepth = d != nullptr;
    }
    return t;
}
C3D_RenderTarget* C3D_RenderTargetCreate(int width, int height, GPU_COLORBUF colorFmt, C3D_DEPTHTYPE depthFmt) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    Target* t = newTarget(width, height, depthFmt);
    void* color = vramAlloc(u32(width) * height * 4);
    if (!color) color = linearAlloc(u32(width) * height * 4);
    if (!color) { delete t; return nullptr; }
    t->ownsColorMem = true;
    t->pub.ownsColor = true;
    C3D_FrameBufColor(&t->pub.frameBuf, color, colorFmt);
    // The colour buffer behaves like a texture the GPU renders into.
    C3D_Tex fake{};
    fake.data = color; fake.width = u16(width); fake.height = u16(height); fake.fmt = GPU_RGBA8;
    fake.size = u32(width) * height * 4; fake.maxLevel = 0;
    t->color = texRegister(&fake);
    if (t->color) t->color->renderTarget = true;
    targets.push_back(t);
    return &t->pub;
}
C3D_RenderTarget* C3D_RenderTargetCreateFromTex(C3D_Tex* tex, GPU_TEXFACE face, int level, C3D_DEPTHTYPE depthFmt) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    (void)level;
    Target* t = newTarget(tex->width, tex->height, depthFmt);
    const int type = (tex->param >> 28) & 7;
    void* color = (type == GPU_TEX_CUBE_MAP || type == GPU_TEX_SHADOW_CUBE) ? tex->cube->data[face] : tex->data;
    C3D_FrameBufColor(&t->pub.frameBuf, color, GPU_COLORBUF(tex->fmt));
    t->color = texFind(color);
    if (!t->color) t->color = texRegister(tex);
    if (t->color) t->color->renderTarget = true;
    targets.push_back(t);
    return &t->pub;
}
void C3D_RenderTargetDelete(C3D_RenderTarget* pub) {
    if (!pub) return;
    std::lock_guard<std::recursive_mutex> g(apiLock);
    Target* t = fromPub(pub);
    for (auto& s : links) for (auto& l : s) if (l == t) l = nullptr;
    if (current == t) current = nullptr;
    submit();
    glRun([t] { glTargetDestroy(*t); });
    for (size_t i = 0; i < targets.size(); ++i) if (targets[i] == t) { targets.erase(targets.begin() + long(i)); break; }
    if (t->ownsColorMem) {
        texForget(pub->frameBuf.colorBuf);
        freeMem(pub->frameBuf.colorBuf);
    }
    if (pub->ownsDepth) freeMem(pub->frameBuf.depthBuf);
    delete t;
}
void C3D_RenderTargetSetOutput(C3D_RenderTarget* pub, gfxScreen_t screen, gfx3dSide_t side, u32 transferFlags) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    const int s = screen == GFX_BOTTOM ? 1 : 0, e = side == GFX_RIGHT ? 1 : 0;
    if (links[s][e]) links[s][e]->pub.linked = false;
    Target* t = pub ? fromPub(pub) : nullptr;
    if (t) {
        for (auto& a : links) for (auto& l : a) if (l == t) l = nullptr;
        t->pub.linked = true; t->pub.screen = screen; t->pub.side = side; t->pub.transferFlags = transferFlags;
    }
    links[s][e] = t;
}

u32 C3D_CalcColorBufSize(u32 width, u32 height, GPU_COLORBUF fmt) {
    static const u8 sizes[] = {4, 3, 2, 2, 2};
    return width * height * sizes[fmt < 5 ? fmt : 0];
}
u32 C3D_CalcDepthBufSize(u32 width, u32 height, GPU_DEPTHBUF fmt) {
    return width * height * (fmt == GPU_RB_DEPTH16 ? 2 : fmt == GPU_RB_DEPTH24 ? 3 : 4);
}
void C3D_FrameBufClear(C3D_FrameBuf* fb, C3D_ClearBits clearBits, u32 clearColor, u32 clearDepth) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    Target* t = reinterpret_cast<Target*>(reinterpret_cast<u8*>(fb) - offsetof(C3D_RenderTarget, frameBuf));
    bool known = false;
    for (Target* x : targets) known |= x == t;
    if (!known) return;
    Cmd c{Cmd::Clear};
    c.target = t; c.clearBits = clearBits; c.clearColor = clearColor; c.clearDepth = clearDepth;
    list.push_back(std::move(c));
}

// ------------------------------------------------------------------ frames
float C3D_FrameRate(float) { return 60.f; }
void C3D_FrameSync(void) { portWaitVBlank(); }
u32 C3D_FrameCounter(int) { return portVBlankCount(); }
bool C3D_FrameBegin(u8 flags) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (inFrame) return false;
    if (flags & C3D_FRAME_SYNCDRAW) C3D_FrameSync();
    glWaitIdle();
    inFrame = true;
    return true;
}
bool C3D_FrameDrawOn(C3D_RenderTarget* pub) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (!inFrame || !pub) return false;
    current = fromPub(pub);
    current->pub.used = true;
    // Like citro3d: drawing on a target resets the viewport to the whole
    // target. Without this the lower screen kept the upper's 240x400
    // viewport and its UI came out stretched and cut off at the edges.
    static const bool noReset = getenv("MKW_NOVPRESET") != nullptr;
    if (!noReset) {
        static const bool log = getenv("MKW_VPLOG") != nullptr;
        if (log) fprintf(stderr, "[vp] drawOn %p %dx%d (was %d,%d,%d,%d)\n", (void*)current, current->width, current->height, cur.vp[0], cur.vp[1], cur.vp[2], cur.vp[3]);
        cur.vp[0] = cur.vp[1] = 0; cur.vp[2] = current->width; cur.vp[3] = current->height;
        stateDirty = true;
    }
    return true;
}
void C3D_FrameSplit(u8) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (!inFrame) return;
    submit();
}
void C3D_FrameEnd(u8) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (!inFrame) return;
    inFrame = false;
    bool shown[2] = {false, false};
    for (int s = 0; s < 2; ++s)
        for (int e = 0; e < 2; ++e) {
            Target* t = links[s][e];
            if (!t || !t->pub.used) continue;
            if (e && !gfxIs3D()) continue;
            const gfxScreen_t screen = s ? GFX_BOTTOM : GFX_TOP;
            if (!e && portSetDirect(s)) {
                Cmd snap{Cmd::Call};
                snap.fn = [t, s] { glSnapshotTarget(*t, s); };
                list.push_back(std::move(snap));
                shown[s] = true;
                continue;
            }
            void* out = gfxGetFramebuffer(screen, e ? GFX_RIGHT : GFX_LEFT, nullptr, nullptr);
            const int fmt = int(t->pub.transferFlags >> 12 & 7);
            Cmd c{Cmd::Call};
            c.fn = [t, out, fmt] { glReadTarget(*t, out, fmt); };
            list.push_back(std::move(c));
            shown[s] = true;
        }
    for (Target* t : targets) t->pub.used = false;
    submit();
    portFrameEndPresent(shown[0], shown[1]);
}
float C3D_GetDrawingTime(void) { return glLastFrameMs(); }
float C3D_GetProcessingTime(void) { return 0.f; }
void C3D_FrameEndHook(void (*)(void*), void*) {}

// ------------------------------------------------------------------ transfers
void C3D_SyncDisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags) {
    (void)outdim;
    Target* t = targetFromColorBuf(inadr);
    if (t) {
        flushRecorded();
        const int fmt = int(flags >> 12 & 7);
        glRun([t, outadr, fmt] { glReadTarget(*t, outadr, fmt); });
    } else {
        const u32 w = indim & 0xFFFF, h = indim >> 16;
        const int inFmt = int(flags >> 8 & 7);
        static const u8 bpp[] = {4, 3, 2, 2, 2};
        memmove(outadr, inadr, size_t(w) * h * bpp[inFmt < 5 ? inFmt : 0]);
    }
    texMarkDirty(outadr, 4 * (outdim & 0xFFFF) * (outdim >> 16));
}
void C3D_SyncTextureCopy(u32* inadr, u32, u32* outadr, u32, u32 size, u32) {
    memmove(outadr, inadr, size);
    texMarkDirty(outadr, size);
}
void C3D_SyncMemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a, u32 buf1v, u32* buf1e, u16 control1) {
    auto fill = [](u32* a, u32 v, u32* e, u16 ctl) {
        if (!a) return;
        const int w = ctl >> 8 & 3;
        u8* p = reinterpret_cast<u8*>(a);
        u8* end = reinterpret_cast<u8*>(e);
        const int n = w == 0 ? 2 : w == 1 ? 3 : 4;
        while (p + n <= end) { memcpy(p, &v, size_t(n)); p += n; }
        texMarkDirty(a, size_t(end - reinterpret_cast<u8*>(a)));
    };
    fill(buf0a, buf0v, buf0e, control0);
    fill(buf1a, buf1v, buf1e, control1);
}

// ------------------------------------------------------------------ lighting
void C3D_LightEnvInit(C3D_LightEnv* env) {
    memset(env, 0, sizeof(*env));
    env->flags = C3DF_LightEnv_Dirty;
    env->conf.lutInput.abs = 0x3F; // all abs until a LUT says otherwise
}
void C3D_LightEnvBind(C3D_LightEnv* env) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    lightEnv = env; stateDirty = true;
}
void C3D_LightEnvMaterial(C3D_LightEnv* env, const C3D_Material* mtl) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    env->material = *mtl; stateDirty = true;
}
void C3D_LightEnvAmbient(C3D_LightEnv* env, float r, float g_, float b) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    env->ambient[0] = r; env->ambient[1] = g_; env->ambient[2] = b; stateDirty = true;
}
void C3D_LightEnvLut(C3D_LightEnv* env, GPU_LIGHTLUTID lutId, GPU_LIGHTLUTINPUT input, bool negative, C3D_LightLut* lut) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    (void)input;
    if (int(lutId) < 0 || int(lutId) >= 6) return;
    env->luts[lutId] = lut;
    if (negative) env->conf.lutInput.abs &= ~(1u << lutId);
    else env->conf.lutInput.abs |= 1u << lutId;
    stateDirty = true;
}
void C3D_LightEnvFresnel(C3D_LightEnv*, GPU_FRESNELSEL) {}
void C3D_LightEnvBumpMode(C3D_LightEnv*, GPU_BUMPMODE) {}
void C3D_LightEnvBumpSel(C3D_LightEnv*, int) {}
void C3D_LightEnvBumpNormalZ(C3D_LightEnv*, bool) {}
void C3D_LightEnvShadowMode(C3D_LightEnv*, u32) {}
void C3D_LightEnvShadowSel(C3D_LightEnv*, int) {}
void C3D_LightEnvClampHighlights(C3D_LightEnv*, bool) {}

int C3D_LightInit(C3D_Light* light, C3D_LightEnv* env) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    int i;
    for (i = 0; i < 8; ++i) if (!env->lights[i]) break;
    if (i == 8) return -1;
    env->lights[i] = light;
    memset(light, 0, sizeof(*light));
    light->flags = C3DF_Light_Enabled | C3DF_Light_Dirty;
    light->id = u16(i);
    light->parent = env;
    const float pos[4] = {0, 0, 1, 0}; // directional, towards +z
    memcpy(light->conf.position, pos, sizeof pos);
    stateDirty = true;
    return i;
}
void C3D_LightEnable(C3D_Light* light, bool enable) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    if (enable) light->flags |= C3DF_Light_Enabled; else light->flags &= ~C3DF_Light_Enabled;
    stateDirty = true;
}
void C3D_LightTwoSideDiffuse(C3D_Light*, bool) {}
void C3D_LightGeoFactor(C3D_Light*, int, bool) {}
#define LIGHT_COLOR(fn, field) \
    void fn(C3D_Light* light, float r, float g_, float b) { \
        std::lock_guard<std::recursive_mutex> g(apiLock); \
        light->field[0] = r; light->field[1] = g_; light->field[2] = b; stateDirty = true; \
    }
LIGHT_COLOR(C3D_LightAmbient, ambient)
LIGHT_COLOR(C3D_LightDiffuse, diffuse)
LIGHT_COLOR(C3D_LightSpecular0, specular0)
LIGHT_COLOR(C3D_LightSpecular1, specular1)
#undef LIGHT_COLOR
void C3D_LightPosition(C3D_Light* light, C3D_FVec* pos) {
    std::lock_guard<std::recursive_mutex> g(apiLock);
    const float p[4] = {pos->x, pos->y, pos->z, pos->w == 0.f ? 0.f : 1.f};
    memcpy(light->conf.position, p, sizeof p);
    stateDirty = true;
}
void C3D_LightShadowEnable(C3D_Light*, bool) {}
void C3D_LightSpotEnable(C3D_Light*, bool) {}
void C3D_LightSpotDir(C3D_Light*, float, float, float) {}
void C3D_LightSpotLut(C3D_Light*, C3D_LightLut*) {}
void C3D_LightDistAttnEnable(C3D_Light*, bool) {}
void C3D_LightDistAttn(C3D_Light*, C3D_LightLutDA*) {}

// vendor/citro3d/source/lightlut.c (zlib licence, fincs/citro3d authors)
void LightLut_FromArray(C3D_LightLut* lut, float* data) {
    for (int i = 0; i < 256; i++) {
        float in = data[i], diff = data[i + 256];
        u32 val = 0;
        if (in > 0.0f) { in *= 0x1000; val = (in < 0x1000) ? (u32)in : 0xFFF; }
        u32 val2 = 0;
        if (diff != 0.0f) {
            if (diff < 0) { diff = -diff; val2 = 0x800; }
            diff *= 0x800;
            val2 |= (diff < 0x800) ? (u32)diff : 0x7FF;
        }
        lut->data[i] = val | (val2 << 12);
    }
}
void LightLut_FromFunc(C3D_LightLut* lut, C3D_LightLutFunc func, float param, bool negative) {
    float data[512];
    memset(data, 0, sizeof(data));
    int min = negative ? (-128) : 0;
    int max = negative ? 128 : 256;
    for (int i = min; i <= max; i++) {
        float x = (float)i / max;
        float val = func(x, param);
        int idx = negative ? (i & 0xFF) : i;
        if (i < max) data[idx] = val;
        if (i > min) data[idx + 255] = val - data[idx - 1];
    }
    LightLut_FromArray(lut, data);
}
