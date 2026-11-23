// libctru util/decompress API (used by citro3d's tex3ds.c): GBA/NDS-style
// LZ10, LZ11, RLE and uncompressed streams. Huffman is not used by the
// game's cooked .t3x files and reports failure.
#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>

namespace {
struct Reader {
    decompressCallback cb; void* user; const u8* mem; size_t left;
    u8 buf[4096]; size_t pos = 0, len = 0;
    bool get(u8& out) {
        if (!cb) { if (!left) return false; out = *mem++; --left; return true; }
        if (pos == len) { ssize_t n = cb(user, buf, sizeof buf); if (n <= 0) return false; len = size_t(n); pos = 0; }
        out = buf[pos++]; return true;
    }
    bool read(void* dst, size_t n) { u8* d = static_cast<u8*>(dst); for (size_t i = 0; i < n; ++i) if (!get(d[i])) return false; return true; }
};
struct Writer {
    const decompressIOVec* iov; size_t count, index = 0, offset = 0;
    std::vector<u8> history; // full output, for back references across iovecs
    size_t total() const { size_t t = 0; for (size_t i = 0; i < count; ++i) t += iov[i].size; return t; }
    bool put(u8 b) {
        while (index < count && offset == iov[index].size) { ++index; offset = 0; }
        if (index == count) return false;
        static_cast<u8*>(iov[index].data)[offset++] = b;
        history.push_back(b);
        return true;
    }
    bool copy(size_t disp, size_t n) {
        if (disp > history.size()) return false;
        for (size_t i = 0; i < n; ++i) if (!put(history[history.size() - disp])) return false;
        return true;
    }
};
bool header(Reader& r, decompressType& type, size_t& size) {
    u8 h[4];
    if (!r.read(h, 4)) return false;
    type = decompressType(h[0]);
    size = size_t(h[1]) | size_t(h[2]) << 8 | size_t(h[3]) << 16;
    if (!size) { u8 e[4]; if (!r.read(e, 4)) return false; size = size_t(e[0]) | size_t(e[1]) << 8 | size_t(e[2]) << 16 | size_t(e[3]) << 24; }
    return true;
}
bool lz(Reader& r, Writer& w, size_t size, bool lz11) {
    size_t out = 0;
    while (out < size) {
        u8 flags;
        if (!r.get(flags)) return false;
        for (int bit = 7; bit >= 0 && out < size; --bit) {
            if (!(flags >> bit & 1)) { u8 b; if (!r.get(b) || !w.put(b)) return false; ++out; continue; }
            u8 a, b; if (!r.get(a) || !r.get(b)) return false;
            size_t len, disp;
            if (!lz11) { len = (a >> 4) + 3; disp = (size_t(a & 0xF) << 8 | b) + 1; }
            else {
                switch (a >> 4) {
                case 0: { u8 c; if (!r.get(c)) return false; len = (size_t(a & 0xF) << 4 | b >> 4) + 0x11; disp = (size_t(b & 0xF) << 8 | c) + 1; break; }
                case 1: { u8 c, d; if (!r.get(c) || !r.get(d)) return false; len = (size_t(a & 0xF) << 12 | size_t(b) << 4 | c >> 4) + 0x111; disp = (size_t(c & 0xF) << 8 | d) + 1; break; }
                default: len = (a >> 4) + 1; disp = (size_t(a & 0xF) << 8 | b) + 1; break;
                }
            }
            if (len > size - out) len = size - out;
            if (!w.copy(disp, len)) return false;
            out += len;
        }
    }
    return true;
}
bool rle(Reader& r, Writer& w, size_t size) {
    size_t out = 0;
    while (out < size) {
        u8 f; if (!r.get(f)) return false;
        if (f & 0x80) { size_t n = (f & 0x7F) + 3; u8 b; if (!r.get(b)) return false; for (size_t i = 0; i < n && out < size; ++i, ++out) if (!w.put(b)) return false; }
        else { size_t n = (f & 0x7F) + 1; for (size_t i = 0; i < n && out < size; ++i, ++out) { u8 b; if (!r.get(b) || !w.put(b)) return false; } }
    }
    return true;
}
bool run(int force, const decompressIOVec* iov, size_t iovcnt, decompressCallback cb, void* user, size_t insize) {
    Reader r{cb, user, static_cast<const u8*>(user), insize};
    Writer w{iov, iovcnt};
    decompressType type; size_t size;
    if (!header(r, type, size)) return false;
    if (force >= 0 && int(type) != force) return false;
    if (size > w.total()) size = w.total();
    switch (type) {
    case DECOMPRESS_DUMMY: for (size_t i = 0; i < size; ++i) { u8 b; if (!r.get(b) || !w.put(b)) return false; } return true;
    case DECOMPRESS_LZ10: return lz(r, w, size, false);
    case DECOMPRESS_LZ11: return lz(r, w, size, true);
    case DECOMPRESS_RLE: return rle(r, w, size);
    default: return false;
    }
}
}

extern "C" {
ssize_t decompressCallback_FD(void* userdata, void* buffer, size_t size) { return read(*static_cast<int*>(userdata), buffer, size); }
ssize_t decompressCallback_Stdio(void* userdata, void* buffer, size_t size) { return ssize_t(fread(buffer, 1, size, static_cast<FILE*>(userdata))); }
ssize_t decompressHeader(decompressType* type, size_t* size, decompressCallback cb, void* user, size_t insize) {
    Reader r{cb, user, static_cast<const u8*>(user), insize};
    decompressType t; size_t s;
    if (!header(r, t, s)) return -1;
    if (type) *type = t;
    if (size) *size = s;
    return s >= 0x1000000 ? 8 : 4;
}
bool decompressV(const decompressIOVec* iov, size_t n, decompressCallback cb, void* user, size_t insize) { return run(-1, iov, n, cb, user, insize); }
bool decompressV_LZSS(const decompressIOVec* iov, size_t n, decompressCallback cb, void* user, size_t insize) { return run(DECOMPRESS_LZ10, iov, n, cb, user, insize); }
bool decompressV_LZ11(const decompressIOVec* iov, size_t n, decompressCallback cb, void* user, size_t insize) { return run(DECOMPRESS_LZ11, iov, n, cb, user, insize); }
bool decompressV_RLE(const decompressIOVec* iov, size_t n, decompressCallback cb, void* user, size_t insize) { return run(DECOMPRESS_RLE, iov, n, cb, user, insize); }
bool decompressV_Huff(size_t, const decompressIOVec*, size_t, decompressCallback, void*, size_t) { return false; }
}
