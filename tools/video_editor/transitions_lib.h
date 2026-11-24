// transitions_lib.h — XER-native transition effects registry.
// Hand-written CPU compositor transitions, ids `xer.tr.*`, distinct
// from the libavfilter xfade set (xer_transition_bridge.*) and the
// legacy Type::Crossfade/Wipe/Slide kernels. Several kernels are
// CPU ports of MIT-licensed GL-transitions shaders; provenance per
// kernel is in the .cc and docs/wiki/research/2026-11-24-transitions.md.
//
// Model wiring: Transition::Type::XerFx + Transition::xer_fx (the
// "xer.tr.<name>" id) + Transition::xer_params (a "k=v;k=v" string
// parsed into XerTransitionParams). Unknown ids fall back to the
// native crossfade at render time — same contract as a bad xfade
// token, so a render never dies on a missing transition.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "frame.h"   // video::Frame — packed BGRA8

namespace XER::VideoEditor {

// Param schema shared by every xer.tr.* transition. The semantic of
// `direction` is per-transition (compass index 0..7, axis 0..3, or a
// shape/variant index — see XerTransitionInfo::params + the .cc
// kernel docs); softness/intensity are normalized [0,1]; seed=0
// selects the transition's deterministic default.
struct XerTransitionParams {
    int direction = 0;
    double softness = 0.15;
    double intensity = 1.0;
    std::uint32_t seed = 0;
};

struct XerTransitionInfo {
    const char* id;       // "xer.tr.<name>" — the stored token
    const char* label;    // menu label ("Whip Pan")
    const char* group;    // picker group: Motion|Stylize|Wipe
    const char* params;   // which knobs apply: subset of "dir,soft,int,seed"
};

// The hand-written set, in stable picker order (grouped per `group`,
// ordered wow-per-effort inside each group). Always available — no
// runtime dependency, unlike the xfade bridge.
const std::vector<XerTransitionInfo>& XerTransitions();

// `id` is in XerTransitions().
bool XerTransitionKnown(const std::string& id);

// "dir=3;soft=0.25;int=0.8;seed=7" — unknown keys ignored, missing
// keys keep defaults, non-numeric values keep defaults. Empty string
// == all defaults.
XerTransitionParams ParseXerTransitionParams(const std::string& text);

// Canonical serialization — only non-default fields emitted, so a
// default transition writes "".
std::string FormatXerTransitionParams(const XerTransitionParams& p);

// Blends `over` emerging over `under` at `weight` via the named
// xer.tr transition into `out` (allocated when needed; never aliases
// inputs). Same frame contract as ApplyTransitionPixels: all frames
// canvas-sized packed BGRA8, straight alpha. Endpoints are exact
// short-circuits: weight <= 0 -> copy of `under`, weight >= 1 ->
// copy of `over`, enforced HERE so no kernel can drift at the ends.
// Deterministic: output is a pure function of the inputs + params.
// Returns false on unknown id or frame-contract violations — callers
// fall back to the native crossfade.
bool ApplyXerTransition(const std::string& id,
                        const XerTransitionParams& params,
                        double weight,
                        const video::Frame& under,
                        const video::Frame& over,
                        video::Frame* out);

}  // namespace XER::VideoEditor
