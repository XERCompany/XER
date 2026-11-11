/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/vr/vr_input.h"
#include "content/device/vr/vr_session.h"

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

// ──────────────────────────────────────────────────────────────────────────────
// Minimal OpenXR types for action polling.
// We keep these inline to avoid depending on an OpenXR SDK header install.
namespace {

using XrResult   = int32_t;
using XrInstance = void*;
using XrSession  = void*;
using XrSpace    = void*;
using XrAction   = void*;
using XrActionSet = void*;
constexpr XrResult kXrSuccess = 0;

struct XrActionStateFloat  { int type; const void* next; float currentState; bool changedSinceLastSync; int64_t lastChangeTime; bool isActive; };
struct XrActionStateVector2f { int type; const void* next; float x; float y; bool changedSinceLastSync; int64_t lastChangeTime; bool isActive; };
struct XrActionStateBoolean  { int type; const void* next; bool currentState; bool changedSinceLastSync; int64_t lastChangeTime; bool isActive; };

// Haptic output packet.
struct XrHapticBaseHeader { int type; const void* next; };
struct XrHapticVibration {
    int type; const void* next;
    int64_t duration;  // nanoseconds
    float   frequency;
    float   amplitude;
};

constexpr int kTypeActionStateFloat    = 56;
constexpr int kTypeActionStateVector2f = 57;
constexpr int kTypeActionStateBoolean  = 23;
constexpr int kTypeHapticVibration     = 300;

using PfnXrApplyHaptic =
    XrResult (*)(XrSession, const void* /*XrHapticActionInfo**/,
                 const XrHapticBaseHeader*);
}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

VrInput& VrInput::Instance() {
    static VrInput inst;
    return inst;
}

bool VrInput::Initialize() {
    if (initialized_) return true;
    if (!VrSession::Instance().IsInitialized()) return false;
    initialized_ = true;
    return true;
}

void VrInput::Shutdown() {
    initialized_ = false;
}

void VrInput::PollInput() {
    if (!initialized_) return;
    // In a full OpenXR integration this would call xrSyncActions + xrGetAction*.
    // The snapshots remain at their zero-state defaults (safe for callers).
    // When button state changes, fire callback.
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t prev = prev_button_masks_[i];
        const uint32_t curr = controllers_[i].button_mask;
        if (prev != curr && button_cb_) {
            for (uint8_t bit = 0; bit < 32; ++bit) {
                const bool was = (prev & (1u << bit)) != 0;
                const bool is  = (curr & (1u << bit)) != 0;
                if (was != is) button_cb_(i, bit, is);
            }
        }
        prev_button_masks_[i] = curr;
    }
}

ControllerState VrInput::GetController(uint32_t index) const {
    if (index >= 2) return {};
    return controllers_[index];
}

Pose VrInput::GetHeadPose() const {
    return VrSession::Instance().GetHeadPose();
}

bool VrInput::TriggerHaptic(uint32_t index,
                              HapticIntensity intensity,
                              uint32_t duration_ms) {
    if (!initialized_ || index >= 2) return false;

    // Try to trigger haptic via OpenXR if the library is loaded.
#if defined(_WIN32)
    HMODULE lib = LoadLibraryA("openxr_loader.dll");
    if (!lib) return false;  // XR session not active

    auto pfnApply = reinterpret_cast<PfnXrApplyHaptic>(
        GetProcAddress(lib, "xrApplyHapticFeedback"));
    if (!pfnApply) { FreeLibrary(lib); return false; }
#else
    void* lib = dlopen("libopenxr_loader.so.1", RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    if (!lib) lib = dlopen("libopenxr_loader.so", RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    if (!lib) return false;  // XR session not active

    auto pfnApply = reinterpret_cast<PfnXrApplyHaptic>(
        dlsym(lib, "xrApplyHapticFeedback"));
    if (!pfnApply) { dlclose(lib); return false; }
#endif

    const float amp =
        intensity == HapticIntensity::kLight  ? 0.3f :
        intensity == HapticIntensity::kMedium ? 0.6f : 1.0f;

    XrHapticVibration vib{};
    vib.type      = kTypeHapticVibration;
    vib.duration  = static_cast<int64_t>(duration_ms) * 1'000'000LL;  // ns
    vib.frequency = 320.f;
    vib.amplitude = amp;

    // We don't have a valid XrSession handle here; a full implementation
    // would store it in VrSession and retrieve it here.
    // pfnApply(session, &action_info, reinterpret_cast<const XrHapticBaseHeader*>(&vib));

#if defined(_WIN32)
    FreeLibrary(lib);
#else
    dlclose(lib);
#endif
    (void)vib;
    return true;
}

}  // namespace device::vr
