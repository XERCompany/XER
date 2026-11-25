// Host-side builtin effects — the first-party kernels that used to
// ship as the xer_fx_* / xer_audio_fx plugin DLLs under
// tools/video_editor/plugins/ (src/-API reuse audit strategy (c):
// implement `builtin:` effects host-side, retire the DLL kernels to
// third-party-only).
//
// The descriptors below keep the exact ids ("xer.fx.*", "xer.af.*"),
// kinds, names and positional-param semantics of the retired DLL
// entries, so existing .xerproj clip effects of type "plugin:<id>"
// keep resolving unchanged; "builtin:<id>" resolves them explicitly.
// Where the Skia bridge / XER_AudioCore exposes a semantic
// equivalent, the impl routes there; everything else is the original
// kernel ported verbatim (param defaults and clamp ranges preserved).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "plugin_host.h"

namespace XER::VideoEditor::BuiltinFx {

// Static descriptor table — one PluginDesc per migrated effect,
// `builtin` set true, `file` "builtin". Function pointers target
// this module; no DLL is involved.
const std::vector<PluginDesc>& Effects();

// Convenience for the smoke checks: canonical id list (bare ids,
// no "plugin:"/"builtin:" prefix).
std::vector<std::string> Ids();

// ---- film-look kit ----
// Time-aware film effects (film_grain, gate_weave, light_leak,
// film_flicker, dust_scratch) are dispatched by the compositor with
// the clip's media time and a per-clip seed so their noise/motion
// fields are a deterministic function of (clip, time) — animated
// frame to frame without strobing, and still safe for the
// effect-frame cache (media time is already the cache key's time
// component). IsFilmEffectId reports membership; ApplyFilmEffect is
// the compositor entry point. Their ABI fns render the same kernel
// at t=0/seed=0 so generic PluginHost::ApplyVideo paths still get a
// real (static) effect.
bool IsFilmEffectId(const std::string& id);
bool ApplyFilmEffect(const std::string& id, void* bgra, int width,
                     int height, int stride,
                     const std::vector<float>& params,
                     double media_time_s, std::uint64_t clip_seed);

}  // namespace XER::VideoEditor::BuiltinFx
