// gpu/angle_probe.h — ANGLE bring-up probe (chromium-render-port R1).
//
// Proves/disproves that the staged libEGL.dll/libGLESv2.dll in bin/
// are real ANGLE-on-D3D11 builds capable of initializing a D3D11
// device and rasterizing on this machine. Loads both DLLs at runtime
// (no import libs ship with the staged binaries), brings up a
// pbuffer + GLES2 context, draws a triangle, and verifies the pixel.
//
// Wired in main.cc behind XER_VE_GPU_PROBE=1 — see angle_probe.cc
// for the EGL/GLES declarations (no Khronos headers in-tree/vcpkg).
#pragma once

#include <cstdio>
#include <string>

namespace XER::VideoEditor::Gpu {

struct AngleProbeResult {
    bool egl_dlls_loaded = false;
    // eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, TYPE_D3D11)
    // produced a working display+context.
    bool platform_angle_d3d11_works = false;
    // Plain eglGetDisplay(EGL_DEFAULT_DISPLAY) produced a working
    // display+context (used only when the D3D11 path fails).
    bool default_platform_works = false;
    // Triangle draw + glReadPixels center-pixel color verified.
    bool triangle_verified = false;
    // eglQueryDisplayAttribEXT(EGL_DEVICE_EXT) +
    // eglQueryDeviceAttribEXT(EGL_D3D11_DEVICE_ANGLE) succeeded —
    // nonzero handle in d3d11_device_ptr_hex.
    bool device_query_works = false;
    std::string d3d11_device_ptr_hex;
    // Display extension strings (EGL_EXTENSIONS on the display).
    bool ext_d3d_share_handle = false;   // EGL_ANGLE_d3d_share_handle_client_buffer
    bool ext_stream_producer_d3d = false; // EGL_ANGLE_stream_producer_d3d_texture
    bool ext_device_d3d = false;          // EGL_ANGLE_device_d3d
    bool ext_image_d3d11 = false;         // EGL_ANGLE_image_d3d11_texture
    bool ext_surf_d3d_share_handle = false; // EGL_ANGLE_surface_d3d_texture_2d_share_handle
    bool ext_platform_angle = false;      // client: EGL_ANGLE_platform_angle(_d3d)
    bool ext_device_creation_d3d11 = false; // client: EGL_ANGLE_device_creation_d3d11
    // Which platform variant the GL strings below came from:
    // "d3d11", "default", or "" (no context).
    std::string active_platform;
    std::string renderer_string;  // glGetString(GL_RENDERER)
    std::string vendor_string;    // glGetString(GL_VENDOR)
    std::string version_string;   // glGetString(GL_VERSION)
    std::string egl_version;      // eglQueryString(dpy, EGL_VERSION)
    std::string egl_client_apis;  // eglQueryString(dpy, EGL_CLIENT_APIS)
    // Bring-up diagnostics: which fallback config-attrib set won, the
    // chosen config's EGL_SURFACE_TYPE, the first eglCreatePbuffer-
    // Surface error (0 = first try OK), and the realized surface size.
    int config_attrib_set = -1;
    int cfg_surface_type = -1;
    int first_surf_error = 0;
    int surf_w = -1;
    int surf_h = -1;
    std::string error_detail;
};

// Loads libEGL/libGLESv2 next to the exe, attempts the D3D11 platform
// path first, falls back to the default display, and — on the first
// platform that yields a current ES2 context — draws and verifies a
// red triangle on a 64x64 pbuffer.
AngleProbeResult RunAngleProbe();

// Human-readable block on `out` (stderr at the call site).
void PrintAngleProbeResult(const AngleProbeResult& r, std::FILE* out);

}  // namespace XER::VideoEditor::Gpu
