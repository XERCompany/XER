// xer_transition_bridge.h — FFmpeg `xfade`-backed transition renderer.
// libavfilter's xfade ships ~58 named blend kernels (vs the ~11
// native ones in compositor.cc); this bridge wraps a cached
// two-input AVFilterGraph per (transition, canvas) and evaluates one
// blend step per call:
//
//   buffersrc(BGRA8) ── format=gbrp ─┐
//                                    xfade ── format=bgra ── buffersink
//   buffersrc(BGRA8) ── format=gbrp ─┘
//
// (gbrp = planar RGB — the only 8-bit RGB pixel format xfade
// accepts; the BGRA<->gbrp hops are the price of staying out of the
// YUV round trip that would shift colors.)
//
// Progress is driven through the first input's PTS: the graph is
// primed once with a pts=0 pair (fixing xfade's internal
// start_pts/inputs_offset_pts at 0), after which pushing the pair at
// pts = weight*1e6 produces the blend at that weight. Output pts is
// verified against the pushed value — a mismatch (state drift after
// a backward scrub or an off-contract producer) rebuilds the graph
// once before falling back.
#pragma once

#include <string>
#include <vector>

#include "frame.h"   // video::Frame — packed BGRA8

namespace XER::VideoEditor {

struct XfadeTransitionInfo {
    const char* name;    // avfilter transition token ("radial")
    const char* label;   // menu label ("Radial")
    const char* group;   // picker group: Basic|Wipe|Slide|Geometric|Fancy
};

// The xfade transitions this bridge exposes, in stable picker order
// (grouped per `group`). Compiled-in list — the libavfilter enum is
// stable within FFmpeg 8/9 and every entry is probed against
// avfilter_get_by_name("xfade") at first use.
const std::vector<XfadeTransitionInfo>& XfadeTransitions();

// libavfilter runtime + the xfade filter itself. False on builds
// where the FFmpeg bridge was compiled without avfilter — callers
// must keep the native-blend fallback.
bool XfadeAvailable();

// `name` is in XfadeTransitions() AND xfade itself is present.
bool XfadeTypeAvailable(const std::string& name);

// Blends `over` emerging over `under` at `weight` via the named
// xfade transition into `out` (allocated when needed; never aliases
// inputs). All frames are canvas-sized packed BGRA8, matching
// ApplyTransitionPixels' contract. Endpoints are exact short-circuits:
// weight <= 0 -> copy of `under`, weight >= 1 -> copy of `over`.
// Returns false (with an optional reason) when the type is unknown,
// xfade is unavailable, or the graph fails — callers fall back to
// the native blend, never to a failed render.
bool ApplyXfadeTransition(const std::string& name, double weight,
                          const video::Frame& under,
                          const video::Frame& over, video::Frame* out,
                          std::string* error_out = nullptr);

// Drop all cached filter graphs (project close / canvas change
// sweeps). The cache is keyed by (name, w, h) so this is rarely
// needed; exposed for the smoke suite's isolation.
void XfadeGraphCacheClear();

}  // namespace XER::VideoEditor
