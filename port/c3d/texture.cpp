// citro3d textures on GLES2: the PICA-format image stays in CPU memory exactly
// as on the 3DS (games write it, flush it, copy it); the GL thread converts
// and uploads it lazily whenever its generation changed.
//
// Layout (3dbrew "GPU/Textures", as Citra): 8x8 tiles in row-major order from
// the top of the image, texels Morton-ordered inside each tile. The upload
// keeps memory row 0 as GL row 0, so samplers flip v (see glexec.cpp);
// render-target textures are bottom-up in GL and are not flipped.
#include "backend.hpp"
#include "gl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>

using namespace gl;

namespace c3d {

namespace {
std::mutex regLock;
std::map<uintptr_t, TexRef> registry; // by data address

u32 bitsPerTexel(GPU_TEXCOLOR f) {
    switch (f) {
    case GPU_RGBA8: return 32;
    case GPU_RGB8: return 24;
    case GPU_RGBA5551: case GPU_RGB565: case GPU_RGBA4: case GPU_LA8: case GPU_HILO8: return 16;
    case GPU_L8: case GPU_A8: case GPU_LA4: case GPU_ETC1A4: return 8;
    case GPU_L4: case GPU_A4: case GPU_ETC1: return 4;
    default: return 0;
    }
}
constexpr u8 mortonX[8] = {0, 1, 4, 5, 16, 17, 20, 21};
constexpr u8 mortonY[8] = {0, 2, 8, 10, 32, 34, 40, 42};
}

TexMem::~TexMem() { if (glTex) glGarbage(glTex); }

TexRef texFind(const void* data) {
    std::lock_guard<std::mutex> g(regLock);
    auto it = registry.find(uintptr_t(data));
    return it == registry.end() ? nullptr : it->second;
}
TexRef texRegister(const C3D_Tex* tex) {
    const bool cube = (C3D_TexGetType(const_cast<C3D_Tex*>(tex)) == GPU_TEX_CUBE_MAP ||
                       C3D_TexGetType(const_cast<C3D_Tex*>(tex)) == GPU_TEX_SHADOW_CUBE);
    void* data = cube ? (tex->cube ? tex->cube->data[0] : nullptr) : tex->data;
    if (!data) return nullptr;
    std::lock_guard<std::mutex> g(regLock);
    auto& slot = registry[uintptr_t(data)];
    if (slot && slot->width == tex->width && slot->height == tex->height && slot->fmt == tex->fmt &&
        slot->maxLevel == tex->maxLevel) return slot;
    auto t = std::make_shared<TexMem>();
    t->data = data;
    t->width = tex->width; t->height = tex->height;
    t->fmt = tex->fmt; t->maxLevel = tex->maxLevel; t->cube = cube;
    t->bytes = C3D_TexCalcTotalSize(tex->size, tex->maxLevel);
    slot = t;
    return t;
}
void texForget(const void* data) {
    std::lock_guard<std::mutex> g(regLock);
    registry.erase(uintptr_t(data));
}
void texMarkDirty(const void* addr, size_t size) {
    const uintptr_t lo = uintptr_t(addr), hi = lo + size;
    std::lock_guard<std::mutex> g(regLock);
    auto it = registry.upper_bound(lo);
    if (it != registry.begin()) --it;
    for (; it != registry.end() && it->first < hi; ++it) {
        TexMem& t = *it->second;
        if (it->first + t.bytes > lo && !t.renderTarget) t.generation++;
    }
}

// ------------------------------------------------------------------ conversion
namespace {
struct GLFmt { GLenum format, type; int bpp; bool compressed; };

GLFmt glFormatFor(GPU_TEXCOLOR f) {
    switch (f) {
    case GPU_RGBA8: return {GL_RGBA, GL_UNSIGNED_BYTE, 4, false};
    case GPU_RGB8: case GPU_HILO8: return {GL_RGB, GL_UNSIGNED_BYTE, 3, false};
    case GPU_RGBA5551: return {GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2, false};
    case GPU_RGB565: return {GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2, false};
    case GPU_RGBA4: return {GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2, false};
    case GPU_LA8: case GPU_LA4: return {GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, 2, false};
    case GPU_L8: case GPU_L4: return {GL_LUMINANCE, GL_UNSIGNED_BYTE, 1, false};
    case GPU_A8: case GPU_A4: return {GL_ALPHA, GL_UNSIGNED_BYTE, 1, false};
    case GPU_ETC1: if (hasETC1) return {GL_ETC1_RGB8_OES, 0, 0, true}; return {GL_RGB, GL_UNSIGNED_BYTE, 3, false};
    case GPU_ETC1A4: default: return {GL_RGBA, GL_UNSIGNED_BYTE, 4, false};
    }
}

// ETC1 block decode (Ericsson Texture Compression, as specified for
// OES_compressed_ETC1_RGB8_texture). v is the block as a 64-bit big-endian
// word, which is how the 3DS stores it in little-endian order.
const int etcModifiers[8][2] = {{2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 183}};
inline u8 clamp8(int v) { return u8(v < 0 ? 0 : v > 255 ? 255 : v); }
void etc1Block(u64 v, u8 out[4][4][3]) { // out[y][x]
    const bool diff = v >> 33 & 1, flip = v >> 32 & 1;
    int base[2][3];
    if (diff) {
        for (int c = 0; c < 3; ++c) {
            const int sh = 59 - 8 * c;
            int b = int(v >> sh & 0x1F);
            int d = int(v >> (sh - 3) & 7); if (d >= 4) d -= 8;
            int b2 = b + d;
            base[0][c] = (b << 3) | (b >> 2);
            base[1][c] = ((b2 & 0x1F) << 3) | ((b2 & 0x1F) >> 2);
        }
    } else {
        for (int c = 0; c < 3; ++c) {
            const int sh = 60 - 8 * c;
            base[0][c] = int(v >> sh & 0xF) * 17;
            base[1][c] = int(v >> (sh - 4) & 0xF) * 17;
        }
    }
    const int table[2] = {int(v >> 37 & 7), int(v >> 34 & 7)};
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y) {
            const int i = x * 4 + y;
            const int sub = flip ? (y >= 2) : (x >= 2);
            const int lsb = int(v >> i & 1), msb = int(v >> (i + 16) & 1);
            int m = etcModifiers[table[sub]][lsb];
            if (msb) m = -m;
            for (int c = 0; c < 3; ++c) out[y][x][c] = clamp8(base[sub][c] + m);
        }
}
inline u64 load64(const u8* p) { u64 v; memcpy(&v, p, 8); return v; }
inline u64 bswap64(u64 v) { return __builtin_bswap64(v); }

// One level: PICA tiled -> GL rows (row 0 = image top).
void convertLevel(GPU_TEXCOLOR f, const u8* src, int w, int h, std::vector<u8>& out) {
    const GLFmt gf = glFormatFor(f);
    if (gf.compressed) { // ETC1 native: linear block order, big-endian blocks
        const int bw = w / 4, bh = h / 4;
        out.resize(size_t(bw) * bh * 8);
        const u8* p = src;
        for (int ty = 0; ty < h / 8; ++ty)
            for (int tx = 0; tx < w / 8; ++tx)
                for (int b = 0; b < 4; ++b, p += 8) {
                    const int bx = tx * 2 + (b & 1), by = ty * 2 + (b >> 1);
                    const u64 be = bswap64(load64(p));
                    memcpy(&out[(size_t(by) * bw + bx) * 8], &be, 8);
                }
        return;
    }
    const int bpp = gf.bpp;
    out.resize(size_t(w) * h * bpp);
    if (f == GPU_ETC1 || f == GPU_ETC1A4) {
        const bool alpha = f == GPU_ETC1A4;
        const u8* p = src;
        u8 px[4][4][3];
        for (int ty = 0; ty < h / 8; ++ty)
            for (int tx = 0; tx < w / 8; ++tx)
                for (int b = 0; b < 4; ++b) {
                    u64 a = ~0ull;
                    if (alpha) { a = load64(p); p += 8; }
                    etc1Block(load64(p), px); p += 8;
                    const int bx = tx * 8 + (b & 1) * 4, by = ty * 8 + (b >> 1) * 4;
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 4; ++x) {
                            u8* o = &out[(size_t(by + y) * w + bx + x) * bpp];
                            o[0] = px[y][x][0]; o[1] = px[y][x][1]; o[2] = px[y][x][2];
                            if (alpha) o[3] = u8((a >> (4 * (x * 4 + y)) & 0xF) * 17);
                        }
                }
        return;
    }
    const u32 bits = bitsPerTexel(f);
    const size_t tileBytes = 64 * bits / 8;
    for (int ty = 0; ty < h / 8; ++ty)
        for (int tx = 0; tx < w / 8; ++tx) {
            const u8* tile = src + (size_t(ty) * (w / 8) + tx) * tileBytes;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const int i = mortonX[x] | mortonY[y];
                    u8* o = &out[(size_t(ty * 8 + y) * w + tx * 8 + x) * bpp];
                    switch (f) {
                    case GPU_RGBA8: { const u8* s = tile + i * 4; o[0] = s[3]; o[1] = s[2]; o[2] = s[1]; o[3] = s[0]; break; }
                    case GPU_RGB8: { const u8* s = tile + i * 3; o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; break; }
                    case GPU_HILO8: { const u8* s = tile + i * 2; o[0] = s[1]; o[1] = s[0]; o[2] = 0; break; }
                    case GPU_RGBA5551: case GPU_RGB565: case GPU_RGBA4: memcpy(o, tile + i * 2, 2); break;
                    case GPU_LA8: { const u8* s = tile + i * 2; o[0] = s[1]; o[1] = s[0]; break; }
                    case GPU_LA4: { const u8 s = tile[i]; o[0] = u8((s >> 4) * 17); o[1] = u8((s & 0xF) * 17); break; }
                    case GPU_L8: case GPU_A8: o[0] = tile[i]; break;
                    case GPU_L4: case GPU_A4: { const u8 s = tile[i >> 1]; o[0] = u8(((i & 1) ? s >> 4 : s & 0xF) * 17); break; }
                    default: memset(o, 0, bpp); break;
                    }
                }
        }
}

// Below 8x8 (the PICA minimum) GLES2 still needs every level down to 1x1.
void shrinkLevel(const std::vector<u8>& in, int w, int h, int bpp, std::vector<u8>& out) {
    const int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
    out.resize(size_t(nw) * nh * bpp);
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x)
            memcpy(&out[(size_t(y) * nw + x) * bpp], &in[(size_t(y * (h / nh)) * w + x * (w / nw)) * bpp], bpp);
}
// A solid ETC1 block (individual mode, both halves = colour, modifier +2).
u64 etc1Solid(const u8 rgb[3]) {
    u64 v = 0;
    for (int c = 0; c < 3; ++c) {
        const u64 n = u64((rgb[c] + 8) / 17 > 15 ? 15 : (rgb[c] + 8) / 17);
        v |= n << (60 - 8 * c) | n << (56 - 8 * c);
    }
    return bswap64(v); // stored big-endian for GL
}
}

namespace {
// MKW_TEXLOG=1: once a second, how many textures were uploaded and how long it took.
struct UploadStats {
    bool on = getenv("MKW_TEXLOG") != nullptr;
    unsigned count = 0, maxW = 0, maxH = 0; double bytes = 0, secs = 0, last = 0;
    static double now() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
    void add(const TexMem& t, double dt) {
        ++count; bytes += t.bytes; secs += dt;
        if (t.width * t.height > maxW * maxH) { maxW = t.width; maxH = t.height; }
        const double n = now();
        if (n - last >= 1) {
            fprintf(stderr, "[tex] %u uploads, %.1f MB, %.2fs, largest %ux%u fmt %d\n", count, bytes / 1048576, secs, maxW, maxH, int(t.fmt));
            count = maxW = maxH = 0; bytes = secs = 0; last = n;
        }
    }
} uploadStats;
}

void texUploadImpl(TexMem& t);
void texUpload(TexMem& t) {
    if (!uploadStats.on) { texUploadImpl(t); return; }
    const double t0 = UploadStats::now();
    fprintf(stderr, "[tex] upload %ux%u fmt %d levels %d ...", t.width, t.height, int(t.fmt), t.maxLevel + 1);
    texUploadImpl(t);
    fprintf(stderr, " %.3fs\n", UploadStats::now() - t0);
    uploadStats.add(t, UploadStats::now() - t0);
}

void texUploadImpl(TexMem& t) {
    const u32 gen = t.generation.load();
    if (t.renderTarget) return;
    if (!t.glTex) glGenTextures(1, &t.glTex);
    glBindTexture(GL_TEXTURE_2D, t.glTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const GLFmt gf = glFormatFor(t.fmt);
    const u32 base = t.width * t.height * bitsPerTexel(t.fmt) / 8;
    std::vector<u8> level, small;
    int w = t.width, h = t.height, lv = 0;
    for (; lv <= t.maxLevel; ++lv, w /= 2, h /= 2) {
        const u8* src = static_cast<const u8*>(t.data) + (lv ? C3D_TexCalcTotalSize(base, lv - 1) : 0);
        convertLevel(t.fmt, src, w, h, level);
        if (gf.compressed) glCompressedTexImage2D(GL_TEXTURE_2D, lv, gf.format, w, h, 0, GLsizei(level.size()), level.data());
        else glTexImage2D(GL_TEXTURE_2D, lv, gf.format, w, h, 0, gf.format, gf.type, level.data());
        if (lv == t.maxLevel) break;
    }
    if (t.maxLevel > 0) {
        // Complete the chain below the last PICA level.
        if (gf.compressed) {
            // Solid blocks of the smallest level's first-block average.
            const u8* src = static_cast<const u8*>(t.data) + (lv ? C3D_TexCalcTotalSize(base, lv - 1) : 0);
            u8 px[4][4][3]; etc1Block(load64(src), px);
            int s[3] = {};
            for (auto& row : px) for (auto& p : row) for (int c = 0; c < 3; ++c) s[c] += p[c];
            const u8 avg[3] = {u8(s[0] / 16), u8(s[1] / 16), u8(s[2] / 16)};
            const u64 blk = etc1Solid(avg);
            std::vector<u64> blocks;
            for (int l = lv + 1; w > 1 || h > 1; ++l) {
                w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1;
                blocks.assign(size_t((w + 3) / 4) * ((h + 3) / 4), blk);
                glCompressedTexImage2D(GL_TEXTURE_2D, l, gf.format, w, h, 0, GLsizei(blocks.size() * 8), blocks.data());
            }
        } else {
            for (int l = lv + 1; w > 1 || h > 1; ++l) {
                shrinkLevel(level, w, h, gf.bpp, small);
                w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1;
                level.swap(small);
                glTexImage2D(GL_TEXTURE_2D, l, gf.format, w, h, 0, gf.format, gf.type, level.data());
            }
        }
    }
    t.glLevels = t.maxLevel + 1;
    t.uploaded = gen;
    t.sampler = ~0u;
}

void texApplySampler(TexMem& t, u32 param) {
    const u32 key = param & (GPU_TEXTURE_MAG_FILTER(1) | GPU_TEXTURE_MIN_FILTER(1) | GPU_TEXTURE_MIP_FILTER(1) |
                             GPU_TEXTURE_WRAP_S(3) | GPU_TEXTURE_WRAP_T(3));
    if (t.sampler == key) return;
    t.sampler = key;
    static const GLenum wrap[4] = {GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT};
    const bool mag = param >> 1 & 1, min = param >> 2 & 1, mip = param >> 24 & 1;
    GLenum minf = min ? GL_LINEAR : GL_NEAREST;
    if (t.glLevels > 1 && !t.renderTarget)
        minf = min ? (mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : (mip ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
    // Non-power-of-two render targets only support clamping in GLES2.
    const bool npot = (t.width & (t.width - 1)) || (t.height & (t.height - 1));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, npot ? GL_CLAMP_TO_EDGE : wrap[param >> 12 & 3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, npot ? GL_CLAMP_TO_EDGE : wrap[param >> 8 & 3]);
}

unsigned texLutGL(const std::vector<float>* lut) {
    static std::map<std::vector<float>, unsigned> cache; // by contents
    auto it = cache.find(*lut);
    if (it != cache.end()) return it->second;
    u8 bytes[256];
    for (int i = 0; i < 256; ++i) { float v = (*lut)[i]; bytes[i] = u8(v <= 0 ? 0 : v >= 1 ? 255 : int(v * 255.f + .5f)); }
    unsigned tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, bytes);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    cache[*lut] = tex;
    return tex;
}
}
