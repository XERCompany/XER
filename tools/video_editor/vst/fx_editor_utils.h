// fx_editor_utils.h — pure helpers for the VST3 editor-session path
// (ui/vst3_editor.cc + the app's commit-back on close). Header-only on
// purpose: the logic is unit-testable in XER_VideoEditor_Tests, which
// links the UI-free core and cannot reach the exe's Win32 code.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace XER::VideoEditor::Vst {

// One plugin-declared parameter as the editor reports it — all values
// normalized [0,1] per the VST3 spec.
struct EditorParamSnapshot {
    std::uint32_t id = 0;
    float value = 0.0f;
    float default_value = 0.0f;
};

// The map to persist into ClipAudioFx::params after an editor
// session: every declared parameter whose current value differs from
// its default by more than `eps`. A param the user touched and
// reverted drops out naturally — the rebuilt chain applies defaults
// for absent ids, which is the value it now holds anyway, so the
// persisted clip stays minimal without losing intent.
inline std::map<std::uint32_t, float> PersistableParamEdits(
    const std::vector<EditorParamSnapshot>& params, float eps = 1e-6f) {
    std::map<std::uint32_t, float> out;
    for (const EditorParamSnapshot& p : params) {
        if (std::fabs(p.value - p.default_value) > eps) {
            out[p.id] = p.value;
        }
    }
    return out;
}

// Client size (pixels) for the editor popup: the plugin-reported
// preferred size wins when both axes are sane, otherwise the
// caller's fallback. The clamp keeps a hostile or broken
// IPlugView::getSize report from spawning a degenerate or
// screen-eating window.
struct EditorClientSize {
    int width = 0;
    int height = 0;
};

inline EditorClientSize PickEditorClientSize(int preferred_w,
                                             int preferred_h,
                                             int fallback_w,
                                             int fallback_h) {
    constexpr int kMin = 64;
    constexpr int kMax = 8192;
    const bool sane = preferred_w >= kMin && preferred_h >= kMin &&
                      preferred_w <= kMax && preferred_h <= kMax;
    EditorClientSize size;
    size.width = sane ? preferred_w : fallback_w;
    size.height = sane ? preferred_h : fallback_h;
    size.width = std::clamp(size.width, kMin, kMax);
    size.height = std::clamp(size.height, kMin, kMax);
    return size;
}

// Screen-space top-left that centers a w×h popup over the parent
// rect, nudged back inside the work area when it fits. A window
// larger than the work area pins to the work rect's top-left — every
// edge stays reachable.
struct EditorPopupPlacement {
    int left = 0;
    int top = 0;
};

inline EditorPopupPlacement CenterPopupOnParent(
    int parent_left, int parent_top, int parent_right, int parent_bottom,
    int w, int h,
    int work_left, int work_top, int work_right, int work_bottom) {
    EditorPopupPlacement p;
    p.left = parent_left + (parent_right - parent_left - w) / 2;
    p.top = parent_top + (parent_bottom - parent_top - h) / 2;
    const int work_w = work_right - work_left;
    const int work_h = work_bottom - work_top;
    if (w <= work_w) {
        p.left = std::clamp(p.left, work_left, work_right - w);
    } else {
        p.left = work_left;
    }
    if (h <= work_h) {
        p.top = std::clamp(p.top, work_top, work_bottom - h);
    } else {
        p.top = work_top;
    }
    return p;
}

}  // namespace XER::VideoEditor::Vst
