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

}  // namespace XER::VideoEditor::BuiltinFx
