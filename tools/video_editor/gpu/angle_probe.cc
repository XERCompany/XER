// gpu/angle_probe.cc — see angle_probe.h.
//
// No Khronos headers ship in-tree or in vcpkg (checked
// third_party/cef, cmake/, C:/vcpkg), and no import .lib exists for
// the staged DLLs — so the handful of EGL/GLES2 types, constants,
// and entry points the probe needs are declared here and resolved
// via LoadLibrary/GetProcAddress (x64: one calling convention).
#include "gpu/angle_probe.h"

#if defined(_WIN32)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>

namespace XER::VideoEditor::Gpu {
namespace {

// ---- Minimal EGL/GLES2 declarations (values from Khronos
// egl.h/eglext.h/eglext_angle.h/gl2.h, ABI-stable since forever).

using EGLBoolean = unsigned int;
using EGLenum = unsigned int;
using EGLint = int;
using EGLAttrib = intptr_t;
using EGLDisplay = void*;
using EGLSurface = void*;
using EGLContext = void*;
using EGLConfig = void*;
using EGLDeviceEXT = void*;

constexpr void* kEglNoDisplay = nullptr;
constexpr void* kEglNoSurface = nullptr;
constexpr void* kEglNoContext = nullptr;
constexpr void* kEglDefaultDisplay = nullptr;
constexpr EGLBoolean kEglFalse = 0;

constexpr EGLint EGL_NONE = 0x3038;
constexpr EGLint EGL_VERSION = 0x3054;
constexpr EGLint EGL_EXTENSIONS = 0x3055;
constexpr EGLint EGL_CLIENT_APIS = 0x308D;
constexpr EGLint EGL_SURFACE_TYPE = 0x3033;
constexpr EGLint EGL_PBUFFER_BIT = 0x0001;
constexpr EGLint EGL_RENDERABLE_TYPE = 0x3040;
constexpr EGLint EGL_OPENGL_ES2_BIT = 0x0004;
constexpr EGLint EGL_RED_SIZE = 0x3024;
constexpr EGLint EGL_GREEN_SIZE = 0x3025;
constexpr EGLint EGL_BLUE_SIZE = 0x3026;
constexpr EGLint EGL_ALPHA_SIZE = 0x3027;
constexpr EGLint EGL_WIDTH = 0x3057;
// 0x3058 aliases EGL_LARGEST_PBUFFER — on this ANGLE build the pbuffer
// attrib parser treats it as a boolean, so EGL_HEIGHT is effectively
// unsettable (surface comes out 0/1px tall). The probe therefore
// anchors on any pbuffer it can get and renders into a real FBO.
constexpr EGLint EGL_HEIGHT = 0x3058;
constexpr EGLint EGL_LARGEST_PBUFFER = 0x3058;
constexpr EGLenum EGL_OPENGL_ES_API = 0x30A0;
constexpr EGLint EGL_CONTEXT_CLIENT_VERSION = 0x3098;

// EGL_ANGLE_platform_angle / _d3d (eglext_angle.h).
constexpr EGLenum EGL_PLATFORM_ANGLE_ANGLE = 0x3202;
constexpr EGLAttrib EGL_PLATFORM_ANGLE_TYPE_ANGLE = 0x3203;
constexpr EGLAttrib EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE = 0x3208;
constexpr EGLAttrib EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE = 0x3209;
constexpr EGLAttrib EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE = 0x320A;

// EGL_EXT_device_base + EGL_ANGLE_device_d3d.
constexpr EGLAttrib EGL_DEVICE_EXT = 0x322C;
constexpr EGLint EGL_D3D11_DEVICE_ANGLE = 0x33A1;

// GLES2.
using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLboolean = unsigned char;
using GLint = int;
using GLsizei = int;
using GLuint = unsigned int;
using GLfloat = float;
using GLubyte = unsigned char;
using GLchar = char;

constexpr GLenum GL_FALSE = 0;
constexpr GLenum GL_TRUE = 1;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_VENDOR = 0x1F00;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;

// ---- Entry points.

#define VE_EGL_FN(X)                                                  \
    X(eglGetError, EGLint, (), ())                                    \
    X(eglGetDisplay, EGLDisplay, (void* native), (native))            \
    X(eglGetPlatformDisplay, EGLDisplay,                              \
      (EGLenum platform, void* native, const EGLAttrib* attribs),     \
      (platform, native, attribs))                                    \
    X(eglGetPlatformDisplayEXT, EGLDisplay,                           \
      (EGLenum platform, void* native, const EGLint* attribs),        \
      (platform, native, attribs))                                    \
    X(eglInitialize, EGLBoolean,                                      \
      (EGLDisplay dpy, EGLint* major, EGLint* minor),                 \
      (dpy, major, minor))                                            \
    X(eglTerminate, EGLBoolean, (EGLDisplay dpy), (dpy))              \
    X(eglBindAPI, EGLBoolean, (EGLenum api), (api))                   \
    X(eglChooseConfig, EGLBoolean,                                    \
      (EGLDisplay dpy, const EGLint* attribs, EGLConfig* configs,     \
       EGLint size, EGLint* num),                                     \
      (dpy, attribs, configs, size, num))                             \
    X(eglGetConfigAttrib, EGLBoolean,                                 \
      (EGLDisplay dpy, EGLConfig cfg, EGLint attr, EGLint* value),    \
      (dpy, cfg, attr, value))                                        \
    X(eglCreatePbufferSurface, EGLSurface,                            \
      (EGLDisplay dpy, EGLConfig cfg, const EGLint* attribs),         \
      (dpy, cfg, attribs))                                            \
    X(eglCreateContext, EGLContext,                                   \
      (EGLDisplay dpy, EGLConfig cfg, EGLContext share,               \
       const EGLint* attribs),                                        \
      (dpy, cfg, share, attribs))                                     \
    X(eglMakeCurrent, EGLBoolean,                                     \
      (EGLDisplay dpy, EGLSurface draw, EGLSurface read,              \
       EGLContext ctx),                                               \
      (dpy, draw, read, ctx))                                         \
    X(eglQuerySurface, EGLBoolean,                                    \
      (EGLDisplay dpy, EGLSurface s, EGLint attr, EGLint* value),     \
      (dpy, s, attr, value))                                          \
    X(eglDestroySurface, EGLBoolean,                                  \
      (EGLDisplay dpy, EGLSurface s), (dpy, s))                       \
    X(eglDestroyContext, EGLBoolean,                                  \
      (EGLDisplay dpy, EGLContext c), (dpy, c))                       \
    X(eglQueryString, const char*, (EGLDisplay dpy, EGLint name),     \
      (dpy, name))                                                    \
    X(eglQueryDisplayAttribEXT, EGLBoolean,                           \
      (EGLDisplay dpy, EGLint attr, EGLAttrib* value),                \
      (dpy, attr, value))                                             \
    X(eglQueryDeviceAttribEXT, EGLBoolean,                            \
      (EGLDeviceEXT dev, EGLint attr, EGLAttrib* value),              \
      (dev, attr, value))

#define VE_GL_FN(X)                                                   \
    X(glGetString, const GLubyte*, (GLenum name), (name))             \
    X(glGetError, GLenum, (), ())                                     \
    X(glViewport, void,                                               \
      (GLint x, GLint y, GLsizei w, GLsizei h), (x, y, w, h))         \
    X(glClearColor, void,                                             \
      (GLfloat r, GLfloat g, GLfloat b, GLfloat a), (r, g, b, a))     \
    X(glClear, void, (GLbitfield mask), (mask))                       \
    X(glCreateShader, GLuint, (GLenum type), (type))                  \
    X(glShaderSource, void,                                           \
      (GLuint sh, GLsizei n, const GLchar* const* strs,               \
       const GLint* lens),                                            \
      (sh, n, strs, lens))                                            \
    X(glCompileShader, void, (GLuint sh), (sh))                       \
    X(glGetShaderiv, void,                                            \
      (GLuint sh, GLenum pname, GLint* out), (sh, pname, out))        \
    X(glGetShaderInfoLog, void,                                       \
      (GLuint sh, GLsizei cap, GLsizei* len, GLchar* buf),            \
      (sh, cap, len, buf))                                            \
    X(glCreateProgram, GLuint, (), ())                                \
    X(glAttachShader, void, (GLuint p, GLuint s), (p, s))             \
    X(glLinkProgram, void, (GLuint p), (p))                           \
    X(glGetProgramiv, void,                                           \
      (GLuint p, GLenum pname, GLint* out), (p, pname, out))          \
    X(glGetProgramInfoLog, void,                                      \
      (GLuint p, GLsizei cap, GLsizei* len, GLchar* buf),             \
      (p, cap, len, buf))                                             \
    X(glUseProgram, void, (GLuint p), (p))                            \
    X(glDeleteShader, void, (GLuint s), (s))                          \
    X(glDeleteProgram, void, (GLuint p), (p))                         \
    X(glGenBuffers, void, (GLsizei n, GLuint* ids), (n, ids))         \
    X(glDeleteBuffers, void, (GLsizei n, const GLuint* ids), (n, ids)) \
    X(glBindBuffer, void, (GLenum t, GLuint id), (t, id))             \
    X(glBufferData, void,                                             \
      (GLenum t, intptr_t size, const void* data, GLenum usage),      \
      (t, size, data, usage))                                         \
    X(glGetAttribLocation, GLint,                                     \
      (GLuint p, const GLchar* name), (p, name))                      \
    X(glEnableVertexAttribArray, void, (GLuint i), (i))               \
    X(glVertexAttribPointer, void,                                    \
      (GLuint i, GLint size, GLenum type, GLboolean norm,             \
       GLsizei stride, const void* ptr),                              \
      (i, size, type, norm, stride, ptr))                             \
    X(glDrawArrays, void,                                             \
      (GLenum mode, GLint first, GLsizei count),                      \
      (mode, first, count))                                           \
    X(glFinish, void, (), ())                                         \
    X(glReadPixels, void,                                             \
      (GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt,            \
       GLenum type, void* px),                                        \
      (x, y, w, h, fmt, type, px))                                    \
    X(glGenTextures, void, (GLsizei n, GLuint* ids), (n, ids))        \
    X(glDeleteTextures, void, (GLsizei n, const GLuint* ids), (n, ids)) \
    X(glBindTexture, void, (GLenum t, GLuint id), (t, id))            \
    X(glTexParameteri, void,                                          \
      (GLenum t, GLenum name, GLint v), (t, name, v))                 \
    X(glTexImage2D, void,                                             \
      (GLenum t, GLint lvl, GLint ifmt, GLsizei w, GLsizei h,         \
       GLint border, GLenum fmt, GLenum type, const void* px),        \
      (t, lvl, ifmt, w, h, border, fmt, type, px))                    \
    X(glGenFramebuffers, void, (GLsizei n, GLuint* ids), (n, ids))    \
    X(glDeleteFramebuffers, void,                                     \
      (GLsizei n, const GLuint* ids), (n, ids))                       \
    X(glBindFramebuffer, void, (GLenum t, GLuint id), (t, id))        \
    X(glFramebufferTexture2D, void,                                   \
      (GLenum t, GLenum att, GLenum tt, GLuint tex, GLint lvl),       \
      (t, att, tt, tex, lvl))                                         \
    X(glCheckFramebufferStatus, GLenum, (GLenum t), (t))

// Declare function-pointer globals.
#define VE_DECL(name, ret, params, args) using Fn_##name = ret (*) params;
VE_EGL_FN(VE_DECL)
VE_GL_FN(VE_DECL)
#undef VE_DECL

struct EglFns {
#define VE_FIELD(name, ret, params, args) Fn_##name name = nullptr;
    VE_EGL_FN(VE_FIELD)
#undef VE_FIELD
    using GetProc = void* (*)(const char*);
    GetProc eglGetProcAddress = nullptr;
};

struct GlFns {
#define VE_FIELD(name, ret, params, args) Fn_##name name = nullptr;
    VE_GL_FN(VE_FIELD)
#undef VE_FIELD
};

bool LoadEgl(HMODULE egl, HMODULE gles, EglFns& f, std::string& err) {
#define VE_LOAD(name, ret, params, args)                              \
    f.name = reinterpret_cast<Fn_##name>(                             \
        GetProcAddress(egl, #name));                                  \
    if (!f.name) {                                                    \
        err = "GetProcAddress(" #name ") failed on libEGL.dll";       \
        return false;                                                 \
    }
    VE_EGL_FN(VE_LOAD)
#undef VE_LOAD
    f.eglGetProcAddress = reinterpret_cast<EglFns::GetProc>(
        GetProcAddress(egl, "eglGetProcAddress"));
    (void)gles;
    return true;
}

bool LoadGl(HMODULE gles, const EglFns& egl, GlFns& f, std::string& err) {
    bool any_missing = false;
    const char* first_missing = nullptr;
#define VE_LOAD(name, ret, params, args)                              \
    f.name = reinterpret_cast<Fn_##name>(                             \
        GetProcAddress(gles, #name));                                 \
    if (!f.name && egl.eglGetProcAddress) {                           \
        f.name = reinterpret_cast<Fn_##name>(                         \
            egl.eglGetProcAddress(#name));                            \
    }                                                                 \
    if (!f.name) {                                                    \
        any_missing = true;                                           \
        if (!first_missing) first_missing = #name;                    \
    }
    VE_GL_FN(VE_LOAD)
#undef VE_LOAD
    if (any_missing) {
        err = std::string("GLES2 entry missing: ") + first_missing;
        return false;
    }
    return true;
}

const char* SafeStr(const char* s) { return s ? s : ""; }

std::string EglErrString(const EglFns& f) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "eglError=0x%x", f.eglGetError());
    return buf;
}

bool HasExt(const char* haystack, const char* needle) {
    return haystack && std::strstr(haystack, needle) != nullptr;
}

// Tears down whatever was created on `dpy`.
struct EglSession {
    const EglFns* f = nullptr;
    EGLDisplay dpy = kEglNoDisplay;
    EGLSurface surface = kEglNoSurface;
    EGLContext ctx = kEglNoContext;
    // Diagnostics for the report (which config set won, whether the
    // NULL-attrib pbuffer retry fired, chosen config's surface type).
    int chosen_attrib_set = -1;
    EGLint cfg_surface_type = -1;
    EGLint cfg_renderable = -1;
    EGLint first_surf_error = 0;
    EGLint surf_w = -1;
    EGLint surf_h = -1;
    ~EglSession() {
        if (!f || dpy == kEglNoDisplay) return;
        if (ctx != kEglNoContext) {
            f->eglMakeCurrent(dpy, kEglNoSurface, kEglNoSurface,
                              kEglNoContext);
            f->eglDestroyContext(dpy, ctx);
        }
        if (surface != kEglNoSurface) f->eglDestroySurface(dpy, surface);
        f->eglTerminate(dpy);
    }
};

// display -> initialize -> config -> pbuffer -> ES2 context -> current.
// On success the session owns a current 64x64 ES2 context.
bool BringUp(const EglFns& f, EGLDisplay dpy, EglSession& s,
             std::string& err) {
    if (dpy == kEglNoDisplay) {
        err = "display = EGL_NO_DISPLAY";
        return false;
    }
    s.dpy = dpy;
    EGLint major = 0, minor = 0;
    if (f.eglInitialize(dpy, &major, &minor) == kEglFalse) {
        err = "eglInitialize failed (" + EglErrString(f) + ")";
        return false;
    }
    if (f.eglBindAPI(EGL_OPENGL_ES_API) == kEglFalse) {
        err = "eglBindAPI(ES) failed (" + EglErrString(f) + ")";
        return false;
    }
    // Try increasingly-permissive config attrib sets; ANGLE is
    // documented to accept all of these, but some builds ignore
    // color sizes or want ES3 in RENDERABLE_TYPE.
    static const EGLint kCfgSets[][14] = {
        {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
         EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
         EGL_NONE},
        {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
         EGL_NONE},
        {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE},
        {EGL_NONE},
    };
    EGLConfig cfg = nullptr;
    EGLint ncfg = 0;
    EGLBoolean chose = kEglFalse;
    int chosen_set = -1;
    for (int i = 0; i < 4 && cfg == nullptr; ++i) {
        chose = f.eglChooseConfig(dpy, kCfgSets[i], &cfg, 1, &ncfg);
        if (chose != kEglFalse && ncfg >= 1 && cfg) {
            chosen_set = i;
        } else {
            cfg = nullptr;
        }
    }
    if (cfg == nullptr) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "eglChooseConfig: chose=%u ncfg=%d (%s)",
                      chose, ncfg, EglErrString(f).c_str());
        err = buf;
        return false;
    }
    s.chosen_attrib_set = chosen_set;
    f.eglGetConfigAttrib(dpy, cfg, EGL_SURFACE_TYPE,
                         &s.cfg_surface_type);
    f.eglGetConfigAttrib(dpy, cfg, EGL_RENDERABLE_TYPE,
                         &s.cfg_renderable);

    const EGLint surf_attribs[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64,
                                   EGL_NONE};
    s.surface = f.eglCreatePbufferSurface(dpy, cfg, surf_attribs);
    s.first_surf_error = f.eglGetError();
    if (s.surface == kEglNoSurface) {
        // 0x3058 rejected as HEIGHT (parsed as boolean
        // LARGEST_PBUFFER): ask for a legal 64x1 anchor instead.
        const EGLint alt_attribs[] = {EGL_WIDTH, 64,
                                      EGL_LARGEST_PBUFFER, 1,
                                      EGL_NONE};
        s.surface = f.eglCreatePbufferSurface(dpy, cfg, alt_attribs);
    }
    if (s.surface == kEglNoSurface) {
        s.surface = f.eglCreatePbufferSurface(dpy, cfg, nullptr);
    }
    if (s.surface == kEglNoSurface) {
        char buf[192];
        std::snprintf(
            buf, sizeof(buf),
            "eglCreatePbufferSurface failed (%s) set=%d surf_type=0x%x "
            "renderable=0x%x",
            EglErrString(f).c_str(), chosen_set, s.cfg_surface_type,
            s.cfg_renderable);
        err = buf;
        return false;
    }
    // Effective size — a NULL-attrib fallback may yield 0x0.
    f.eglQuerySurface(dpy, s.surface, EGL_WIDTH, &s.surf_w);
    f.eglQuerySurface(dpy, s.surface, EGL_HEIGHT, &s.surf_h);
    const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                  EGL_NONE};
    s.ctx = f.eglCreateContext(dpy, cfg, kEglNoContext, ctx_attribs);
    if (s.ctx == kEglNoContext) {
        err = "eglCreateContext(ES2) failed (" + EglErrString(f) + ")";
        return false;
    }
    if (f.eglMakeCurrent(dpy, s.surface, s.surface, s.ctx) == kEglFalse) {
        err = "eglMakeCurrent failed (" + EglErrString(f) + ")";
        return false;
    }
    return true;
}

GLuint Compile(const GlFns& g, GLenum type, const char* src,
               std::string& err) {
    GLuint sh = g.glCreateShader(type);
    const GLchar* strs[] = {src};
    g.glShaderSource(sh, 1, strs, nullptr);
    g.glCompileShader(sh);
    GLint ok = GL_FALSE;
    g.glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return sh;
    char log[1024] = {};
    g.glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
    err = std::string("shader compile failed: ") + log;
    g.glDeleteShader(sh);
    return 0;
}

// Draws a red triangle on black into a 64x64 texture FBO (the pbuffer
// itself may be 0/1px — see EGL_HEIGHT note above), verifies
// center==red and a triangle-outside pixel==black.
bool DrawAndVerify(const GlFns& g, std::string& err) {
    // Render target: 64x64 RGBA texture on an FBO.
    GLuint tex = 0, fbo = 0;
    g.glGenTextures(1, &tex);
    g.glBindTexture(GL_TEXTURE_2D, tex);
    g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                      GL_CLAMP_TO_EDGE);
    g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                      GL_CLAMP_TO_EDGE);
    g.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
    g.glGenFramebuffers(1, &fbo);
    g.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    g.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, tex, 0);
    if (g.glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        err = "FBO incomplete (status=0x" +
              std::to_string(static_cast<unsigned>(
                  g.glCheckFramebufferStatus(GL_FRAMEBUFFER))) +
              ")";
        return false;
    }
    static const char* kVS =
        "attribute vec2 a_pos;\n"
        "void main(){ gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char* kFS =
        "precision mediump float;\n"
        "void main(){ gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";
    GLuint vs = Compile(g, GL_VERTEX_SHADER, kVS, err);
    if (!vs) return false;
    GLuint fs = Compile(g, GL_FRAGMENT_SHADER, kFS, err);
    if (!fs) {
        g.glDeleteShader(vs);
        return false;
    }
    GLuint prog = g.glCreateProgram();
    g.glAttachShader(prog, vs);
    g.glAttachShader(prog, fs);
    g.glLinkProgram(prog);
    GLint ok = GL_FALSE;
    g.glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024] = {};
        g.glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        err = std::string("program link failed: ") + log;
        g.glDeleteShader(vs);
        g.glDeleteShader(fs);
        g.glDeleteProgram(prog);
        return false;
    }
    const GLfloat tri[] = {-1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 1.0f};
    GLuint vbo = 0;
    g.glGenBuffers(1, &vbo);
    g.glBindBuffer(GL_ARRAY_BUFFER, vbo);
    g.glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    GLint loc = g.glGetAttribLocation(prog, "a_pos");
    if (loc < 0) {
        err = "glGetAttribLocation(a_pos) = -1";
        return false;
    }
    g.glViewport(0, 0, 64, 64);
    g.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    g.glClear(GL_COLOR_BUFFER_BIT);
    g.glUseProgram(prog);
    g.glEnableVertexAttribArray(static_cast<GLuint>(loc));
    g.glVertexAttribPointer(static_cast<GLuint>(loc), 2, GL_FLOAT,
                            GL_FALSE, 0, nullptr);
    g.glDrawArrays(GL_TRIANGLES, 0, 3);
    g.glFinish();

    GLubyte px[4] = {};
    g.glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    const bool center_ok =
        px[0] > 250 && px[1] < 5 && px[2] < 5 && px[3] > 250;
    GLubyte corner[4] = {};
    // NDC (-1,1) corner is outside the triangle -> still clear color.
    g.glReadPixels(4, 60, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    const bool corner_ok =
        corner[0] < 5 && corner[1] < 5 && corner[2] < 5;
    const GLenum glerr = g.glGetError();

    g.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g.glDeleteFramebuffers(1, &fbo);
    g.glDeleteTextures(1, &tex);
    g.glDeleteBuffers(1, &vbo);
    g.glDeleteShader(vs);
    g.glDeleteShader(fs);
    g.glDeleteProgram(prog);

    if (glerr != 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "glError=0x%x", glerr);
        err = buf;
        return false;
    }
    if (!center_ok || !corner_ok) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "pixel mismatch: center=(%u,%u,%u,%u) corner="
                      "(%u,%u,%u,%u)",
                      px[0], px[1], px[2], px[3], corner[0], corner[1],
                      corner[2], corner[3]);
        err = buf;
        return false;
    }
    return true;
}

// Records GL identity strings + extension flags on a current context.
void RecordStrings(const EglFns& f, const GlFns& g, EGLDisplay dpy,
                   AngleProbeResult& r) {
    r.vendor_string = SafeStr(
        reinterpret_cast<const char*>(g.glGetString(GL_VENDOR)));
    r.renderer_string = SafeStr(
        reinterpret_cast<const char*>(g.glGetString(GL_RENDERER)));
    r.version_string = SafeStr(
        reinterpret_cast<const char*>(g.glGetString(GL_VERSION)));
    r.egl_version = SafeStr(f.eglQueryString(dpy, EGL_VERSION));
    r.egl_client_apis = SafeStr(f.eglQueryString(dpy, EGL_CLIENT_APIS));

    const char* dpy_exts = f.eglQueryString(dpy, EGL_EXTENSIONS);
    r.ext_d3d_share_handle = HasExt(
        dpy_exts, "EGL_ANGLE_d3d_share_handle_client_buffer");
    r.ext_stream_producer_d3d =
        HasExt(dpy_exts, "EGL_ANGLE_stream_producer_d3d_texture");
    r.ext_device_d3d = HasExt(dpy_exts, "EGL_ANGLE_device_d3d");
    r.ext_image_d3d11 =
        HasExt(dpy_exts, "EGL_ANGLE_image_d3d11_texture");
    r.ext_surf_d3d_share_handle = HasExt(
        dpy_exts, "EGL_ANGLE_surface_d3d_texture_2d_share_handle");
    const char* client_exts =
        f.eglQueryString(kEglNoDisplay, EGL_EXTENSIONS);
    r.ext_platform_angle =
        HasExt(client_exts, "EGL_ANGLE_platform_angle");
    r.ext_device_creation_d3d11 =
        HasExt(client_exts, "EGL_ANGLE_device_creation_d3d11");

    // D3D11 device query (EGL_EXT_device_query + EGL_ANGLE_device_d3d).
    EGLAttrib egl_dev = 0;
    if (f.eglQueryDisplayAttribEXT &&
        f.eglQueryDisplayAttribEXT(
            dpy, static_cast<EGLint>(EGL_DEVICE_EXT), &egl_dev) &&
        egl_dev && f.eglQueryDeviceAttribEXT) {
        EGLAttrib d3d11 = 0;
        if (f.eglQueryDeviceAttribEXT(
                reinterpret_cast<EGLDeviceEXT>(egl_dev),
                EGL_D3D11_DEVICE_ANGLE, &d3d11) &&
            d3d11) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%llx",
                          static_cast<unsigned long long>(d3d11));
            r.d3d11_device_ptr_hex = buf;
            r.device_query_works = true;
        }
    }
}

}  // namespace

AngleProbeResult RunAngleProbe() {
    AngleProbeResult r;
    // DLLs stage next to the exe; LOAD_LIBRARY_SEARCH_DEFAULT_DIRS is
    // implicit (exe dir is already searched by plain LoadLibrary).
    HMODULE gles = LoadLibraryA("libGLESv2.dll");
    HMODULE egl = LoadLibraryA("libEGL.dll");
    if (!egl || !gles) {
        r.error_detail = "LoadLibrary failed: libEGL.dll=" +
                         std::to_string(reinterpret_cast<uintptr_t>(egl)) +
                         " libGLESv2.dll=" +
                         std::to_string(reinterpret_cast<uintptr_t>(gles));
        return r;
    }
    r.egl_dlls_loaded = true;

    EglFns f;
    GlFns g;
    if (!LoadEgl(egl, gles, f, r.error_detail)) return r;
    if (!LoadGl(gles, f, g, r.error_detail)) return r;

    // Attempt 1: Chromium path — EGL_PLATFORM_ANGLE_ANGLE + D3D11.
    {
        EglSession s{&f};
        const EGLAttrib d3d11_attribs[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE,
            EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE,
            EGL_NONE};
        EGLDisplay dpy = kEglNoDisplay;
        if (f.eglGetPlatformDisplay) {
            dpy = f.eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE,
                                          kEglDefaultDisplay,
                                          d3d11_attribs);
        } else if (f.eglGetPlatformDisplayEXT) {
            const EGLint ext_attribs[] = {
                static_cast<EGLint>(EGL_PLATFORM_ANGLE_TYPE_ANGLE),
                static_cast<EGLint>(EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE),
                static_cast<EGLint>(EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE),
                static_cast<EGLint>(
                    EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE),
                EGL_NONE};
            dpy = f.eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE,
                                             kEglDefaultDisplay,
                                             ext_attribs);
        }
        std::string err;
        if (BringUp(f, dpy, s, err)) {
            r.platform_angle_d3d11_works = true;
            r.active_platform = "d3d11";
            r.config_attrib_set = s.chosen_attrib_set;
            r.cfg_surface_type = s.cfg_surface_type;
            r.first_surf_error = s.first_surf_error;
            r.surf_w = s.surf_w;
            r.surf_h = s.surf_h;
            RecordStrings(f, g, dpy, r);
            r.triangle_verified = DrawAndVerify(g, r.error_detail);
            return r;
        }
        r.error_detail = "d3d11 platform: " + err;
    }

    // Attempt 2: default display.
    {
        EglSession s{&f};
        EGLDisplay dpy = f.eglGetDisplay(kEglDefaultDisplay);
        std::string err;
        if (BringUp(f, dpy, s, err)) {
            r.default_platform_works = true;
            r.active_platform = "default";
            r.config_attrib_set = s.chosen_attrib_set;
            r.cfg_surface_type = s.cfg_surface_type;
            r.first_surf_error = s.first_surf_error;
            r.surf_w = s.surf_w;
            r.surf_h = s.surf_h;
            RecordStrings(f, g, dpy, r);
            std::string draw_err;
            r.triangle_verified = DrawAndVerify(g, draw_err);
            if (!draw_err.empty()) {
                // Platform still counts as working; keep the draw
                // error alongside the earlier platform failure note.
                if (!r.error_detail.empty()) r.error_detail += " | ";
                r.error_detail += "default draw: " + draw_err;
            }
            return r;
        }
        if (!r.error_detail.empty()) r.error_detail += " | ";
        r.error_detail += "default platform: " + err;
    }
    return r;
}

void PrintAngleProbeResult(const AngleProbeResult& r, std::FILE* out) {
    std::fprintf(out,
                 "[angle-probe] dlls_loaded=%d d3d11_platform=%d "
                 "default_platform=%d triangle=%d\n",
                 r.egl_dlls_loaded, r.platform_angle_d3d11_works,
                 r.default_platform_works, r.triangle_verified);
    std::fprintf(out, "[angle-probe] active_platform=%s\n",
                 r.active_platform.c_str());
    std::fprintf(out, "[angle-probe] GL_RENDERER=%s\n",
                 r.renderer_string.c_str());
    std::fprintf(out, "[angle-probe] GL_VENDOR=%s\n",
                 r.vendor_string.c_str());
    std::fprintf(out, "[angle-probe] GL_VERSION=%s\n",
                 r.version_string.c_str());
    std::fprintf(out, "[angle-probe] EGL_VERSION=%s CLIENT_APIS=%s\n",
                 r.egl_version.c_str(), r.egl_client_apis.c_str());
    std::fprintf(out,
                 "[angle-probe] ext: d3d_share_handle=%d "
                 "stream_producer_d3d=%d device_d3d=%d "
                 "image_d3d11=%d surf_d3d_share_handle=%d "
                 "platform_angle=%d device_creation_d3d11=%d\n",
                 r.ext_d3d_share_handle, r.ext_stream_producer_d3d,
                 r.ext_device_d3d, r.ext_image_d3d11,
                 r.ext_surf_d3d_share_handle, r.ext_platform_angle,
                 r.ext_device_creation_d3d11);
    std::fprintf(out, "[angle-probe] d3d11_device_query=%d ptr=%s\n",
                 r.device_query_works, r.d3d11_device_ptr_hex.c_str());
    std::fprintf(out,
                 "[angle-probe] cfg_set=%d surf_type=0x%x "
                 "first_surf_err=0x%x surf=%dx%d\n",
                 r.config_attrib_set, r.cfg_surface_type,
                 r.first_surf_error, r.surf_w, r.surf_h);
    if (!r.error_detail.empty()) {
        std::fprintf(out, "[angle-probe] error=%s\n",
                     r.error_detail.c_str());
    }
    std::fflush(out);
}

}  // namespace XER::VideoEditor::Gpu

#else

namespace XER::VideoEditor::Gpu {
AngleProbeResult RunAngleProbe() {
    AngleProbeResult r;
    r.error_detail = "ANGLE probe is Windows-only";
    return r;
}
void PrintAngleProbeResult(const AngleProbeResult& r, std::FILE* out) {
    std::fprintf(out, "[angle-probe] %s\n", r.error_detail.c_str());
}
}  // namespace XER::VideoEditor::Gpu

#endif  // _WIN32
