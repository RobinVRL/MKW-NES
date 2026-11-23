#pragma once
// GLES2/EGL entry points resolved with dlopen at runtime, so the binary links
// without a Mali sysroot (the NES Mini ships libEGL.so/libGLESv2.so from the
// Mali-400 r4p0 driver, or libMali.so providing both).
#define EGL_NO_X11 1
#define GL_GLES_PROTOTYPES 0
#define EGL_EGL_PROTOTYPES 0
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define PORT_GL_FUNCS(X) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) \
    X(PFNGLBINDTEXTUREPROC, glBindTexture) X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLCLEARPROC, glClear) X(PFNGLCLEARCOLORPROC, glClearColor) X(PFNGLCLEARDEPTHFPROC, glClearDepthf) \
    X(PFNGLCLEARSTENCILPROC, glClearStencil) X(PFNGLCOLORMASKPROC, glColorMask) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLCULLFACEPROC, glCullFace) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures) X(PFNGLDEPTHFUNCPROC, glDepthFunc) \
    X(PFNGLDEPTHMASKPROC, glDepthMask) X(PFNGLDISABLEPROC, glDisable) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) X(PFNGLDRAWARRAYSPROC, glDrawArrays) \
    X(PFNGLDRAWELEMENTSPROC, glDrawElements) X(PFNGLENABLEPROC, glEnable) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) X(PFNGLFINISHPROC, glFinish) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLFRONTFACEPROC, glFrontFace) X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) X(PFNGLGENTEXTURESPROC, glGenTextures) \
    X(PFNGLGETERRORPROC, glGetError) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) X(PFNGLGETSTRINGPROC, glGetString) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei) X(PFNGLREADPIXELSPROC, glReadPixels) \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage) X(PFNGLSCISSORPROC, glScissor) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) X(PFNGLSTENCILFUNCPROC, glStencilFunc) \
    X(PFNGLSTENCILMASKPROC, glStencilMask) X(PFNGLSTENCILOPPROC, glStencilOp) \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D) X(PFNGLTEXPARAMETERIPROC, glTexParameteri) \
    X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D) X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLCOPYTEXSUBIMAGE2DPROC, glCopyTexSubImage2D) \
    X(PFNGLUNIFORM1IVPROC, glUniform1iv) X(PFNGLUNIFORM1FPROC, glUniform1f) X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv) X(PFNGLUNIFORM3FVPROC, glUniform3fv) X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f) X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    X(PFNGLVIEWPORTPROC, glViewport) X(PFNGLBLENDCOLORPROC, glBlendColor) \
    X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D) X(PFNGLGETINTEGERVPROC, glGetIntegerv) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation) X(PFNGLFLUSHPROC, glFlush)

#define PORT_EGL_FUNCS(X) \
    X(PFNEGLGETDISPLAYPROC, eglGetDisplay) X(PFNEGLINITIALIZEPROC, eglInitialize) \
    X(PFNEGLCHOOSECONFIGPROC, eglChooseConfig) X(PFNEGLCREATEWINDOWSURFACEPROC, eglCreateWindowSurface) \
    X(PFNEGLCREATEPBUFFERSURFACEPROC, eglCreatePbufferSurface) \
    X(PFNEGLCREATECONTEXTPROC, eglCreateContext) X(PFNEGLMAKECURRENTPROC, eglMakeCurrent) \
    X(PFNEGLSWAPBUFFERSPROC, eglSwapBuffers) X(PFNEGLSWAPINTERVALPROC, eglSwapInterval) \
    X(PFNEGLBINDAPIPROC, eglBindAPI) X(PFNEGLGETERRORPROC, eglGetError) \
    X(PFNEGLDESTROYCONTEXTPROC, eglDestroyContext) X(PFNEGLDESTROYSURFACEPROC, eglDestroySurface) \
    X(PFNEGLTERMINATEPROC, eglTerminate) X(PFNEGLQUERYSURFACEPROC, eglQuerySurface)

namespace gl {
#define PORT_DECL(type, name) extern type name;
PORT_GL_FUNCS(PORT_DECL)
PORT_EGL_FUNCS(PORT_DECL)
#undef PORT_DECL
bool load();
extern bool hasMinMax;    // GL_EXT_blend_minmax
extern bool hasETC1;      // GL_OES_compressed_ETC1_RGB8_texture
extern bool hasPackedDS;  // GL_OES_packed_depth_stencil
extern bool hasDepth24;   // GL_OES_depth24
void readExtensions();    // call with a current context
}
