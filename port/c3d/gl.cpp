#include "gl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

namespace gl {
#define PORT_DEF(type, name) type name = nullptr;
PORT_GL_FUNCS(PORT_DEF)
PORT_EGL_FUNCS(PORT_DEF)
#undef PORT_DEF
bool hasMinMax = false, hasETC1 = false, hasPackedDS = false, hasDepth24 = false;
void readExtensions() {
    const char* e = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!e) e = "";
    hasMinMax = strstr(e, "GL_EXT_blend_minmax") != nullptr;
    hasETC1 = strstr(e, "GL_OES_compressed_ETC1_RGB8_texture") != nullptr && !getenv("MKW_NO_ETC1");
    hasPackedDS = strstr(e, "GL_OES_packed_depth_stencil") != nullptr;
    hasDepth24 = strstr(e, "GL_OES_depth24") != nullptr;
    fprintf(stderr, "[gl] %s / %s; minmax=%d etc1=%d d24s8=%d d24=%d\n",
            (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION),
            hasMinMax, hasETC1, hasPackedDS, hasDepth24);
}

static void* openAny(const char* const* names) {
    for (const char* const* n = names; *n; ++n)
        if (void* h = dlopen(*n, RTLD_NOW | RTLD_GLOBAL)) return h;
    return nullptr;
}

bool load() {
    static const char* eglNames[] = {"libEGL.so", "libEGL.so.1", "libMali.so", nullptr};
    static const char* glesNames[] = {"libGLESv2.so", "libGLESv2.so.2", "libMali.so", nullptr};
    void* egl = openAny(eglNames);
    void* gles = openAny(glesNames);
    if (!egl || !gles) { fprintf(stderr, "[gl] cannot open EGL/GLESv2: %s\n", dlerror()); return false; }
    bool ok = true;
    auto get = [&](void* lib, const char* name) -> void* {
        void* p = dlsym(lib, name);
        if (!p) { fprintf(stderr, "[gl] missing %s\n", name); ok = false; }
        return p;
    };
#define PORT_LOAD_GL(type, name) { void* p = get(gles, #name); memcpy(&name, &p, sizeof p); }
#define PORT_LOAD_EGL(type, name) { void* p = get(egl, #name); memcpy(&name, &p, sizeof p); }
    PORT_GL_FUNCS(PORT_LOAD_GL)
    PORT_EGL_FUNCS(PORT_LOAD_EGL)
#undef PORT_LOAD_GL
#undef PORT_LOAD_EGL
    return ok;
}
}
