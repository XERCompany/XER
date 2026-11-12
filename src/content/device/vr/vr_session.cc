/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/vr/vr_session.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace device::vr {

namespace {

// Minimal OpenXR typedefs needed to detect runtime availability.
using XrResult   = int32_t;
using XrInstance = void*;
struct XrApplicationInfo {
    char applicationName[128];
    uint32_t applicationVersion;
    char engineName[128];
    uint32_t engineVersion;
    uint64_t apiVersion;
};
struct XrInstanceCreateInfo {
    int type; // XR_TYPE_INSTANCE_CREATE_INFO = 1
    const void* next;
    uint64_t createFlags;
    XrApplicationInfo applicationInfo;
    uint32_t enabledApiLayerCount;
    const char* const* enabledApiLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* enabledExtensionNames;
};

using PfnXrCreateInstance =
    XrResult (*)(const XrInstanceCreateInfo*, XrInstance*);
using PfnXrGetInstanceProperties =
    XrResult (*)(XrInstance, void* /*XrInstanceProperties**/);
using PfnXrDestroyInstance = XrResult (*)(XrInstance);

constexpr XrResult kXrSuccess = 0;

#if defined(_WIN32)
// LoadLibrary/GetProcAddress equivalents of the dlopen/dlsym pair used below.
void* XrDlOpen(const char* name) {
    return LoadLibraryA(name);
}
void* XrDlSym(void* lib, const char* sym) {
    return lib ? reinterpret_cast<void*>(
                     GetProcAddress(static_cast<HMODULE>(lib), sym))
               : nullptr;
}
void XrDlClose(void* lib) {
    if (lib) FreeLibrary(static_cast<HMODULE>(lib));
}
#else
void* XrDlOpen(const char* name) { return dlopen(name, RTLD_LAZY | RTLD_LOCAL); }
void* XrDlSym(void* lib, const char* sym) { return dlsym(lib, sym); }
void XrDlClose(void* lib) { if (lib) dlclose(lib); }
#endif

static bool ProbeOpenXR(VrDevice& out) {
#if defined(_WIN32)
    void* lib = XrDlOpen("openxr_loader.dll");
#else
    void* lib = XrDlOpen("libopenxr_loader.so.1");
    if (!lib) lib = XrDlOpen("libopenxr_loader.so");
#endif
    if (!lib) return false;

    auto pfnCreate = reinterpret_cast<PfnXrCreateInstance>(
        XrDlSym(lib, "xrCreateInstance"));
    if (!pfnCreate) { XrDlClose(lib); return false; }

    XrInstanceCreateInfo info{};
    info.type = 1 /*XR_TYPE_INSTANCE_CREATE_INFO*/;
    std::snprintf(info.applicationInfo.applicationName,
                  sizeof(info.applicationInfo.applicationName), "%s", "XER");
    info.applicationInfo.applicationVersion = 1;
    std::snprintf(info.applicationInfo.engineName,
                  sizeof(info.applicationInfo.engineName), "%s", "XER Engine");
    info.applicationInfo.engineVersion = 1;
    info.applicationInfo.apiVersion = (1ULL << 48); // XR_MAKE_VERSION(1,0,0)

    XrInstance instance = nullptr;
    if (pfnCreate(&info, &instance) != kXrSuccess || !instance) {
        XrDlClose(lib);
        return false;
    }

    out.runtime      = VrRuntime::kOpenXR;
    out.name         = "OpenXR HMD";
    out.manufacturer = "OpenXR Runtime";
    out.is_present   = true;
    // Default suggested render resolution; a real integration would query
    // xrEnumerateViewConfigurations.
    out.render_width  = 1440;
    out.render_height = 1600;
    out.refresh_rate  = 90.f;

    auto pfnDestroy = reinterpret_cast<PfnXrDestroyInstance>(
        XrDlSym(lib, "xrDestroyInstance"));
    if (pfnDestroy) pfnDestroy(instance);
    XrDlClose(lib);
    return true;
}

}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

VrSession& VrSession::Instance() {
    static VrSession inst;
    return inst;
}

VrDevice VrSession::GetDevice() const {
    VrDevice dev;
    ProbeOpenXR(dev);
    return dev;
}

bool VrSession::Initialize() {
    if (initialized_) return true;
    VrDevice dev;
    if (ProbeOpenXR(dev)) {
        active_runtime_ = VrRuntime::kOpenXR;
        initialized_ = true;
        return true;
    }
    return false;
}

void VrSession::Shutdown() {
    initialized_ = false;
    active_runtime_ = VrRuntime::kNone;
    if (runtime_handle_) {
        XrDlClose(runtime_handle_);
        runtime_handle_ = nullptr;
    }
}

Pose VrSession::GetHeadPose() const {
    // Returns identity pose; a real integration would call
    // xrLocateViews / xrLocateSpace.
    return Pose{};
}

bool VrSession::BeginFrame() {
    return initialized_;
}

bool VrSession::EndFrame() {
    return initialized_;
}

}  // namespace device::vr
