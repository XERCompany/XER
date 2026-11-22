// Text clip styling — the full text-inspector property set for
// SourceKind::Text generated clips. ClipTextStyle is the struct API
// the UI edits directly; the "text | key=value ..." param string is
// only a persistence format (produced/consumed by the Serialize/Parse
// pair — callers must not string-mangle it).
//
// Param grammar (whitespace-separated key=value tokens after the
// first '|'; the text payload before it cannot contain '|'):
//
//   font=<pct-encoded family>   weight=<100-900|name>  italic=0|1
//   underline=0|1  strike=0|1   size=<px>  tracking=<px>
//   leading=<px, 0=auto>        align=left|center|right
//   valign=top|middle|bottom    x=<px> y=<px>  w=<px> h=<px>
//   radius=<px>  rotate=<deg>   baseline=<px>
//   color=#RRGGBB[AA]  opacity=<0..1 or 0..100>
//   outline=<px>  outline_color=#RRGGBB[AA]   (stroke*/stroke_color
//   aliases accepted)              bg=#RRGGBB[AA]  (box background)
//   shadow=0|1   shadow_x=<px>  shadow_y=<px>  shadow_blur=<px>
//   shadow_color=#RRGGBB[AA]
//
// Karaoke word highlight (animated captions):
//   karaoke=1                      enables per-word coloring
//   karaoke_times=t0-t1,t0-t1,...  word spans in CLIP-LOCAL seconds,
//                                  one pair per whitespace-delimited
//                                  word of the text (alias: kwt)
//   karaoke_active=#RRGGBB[AA]     color of the currently-spoken word
//                                  (alias: kact)
//   karaoke_done=#RRGGBB[AA]       color of already-spoken words;
//                                  omitted/alpha-0 means they keep
//                                  the base `color` (alias: kdone)
//
// A karaoke clip is time-VARIANT: RenderClipText takes the clip-local
// time and colors word i `karaoke_active` while its span contains it,
// `karaoke_done` once it has passed, `color` before it starts.
// Words past the end of the timing list stay inactive — a malformed
// list degrades to the flat base color, never to a missing glyph.
//
// Legacy tokens size/color/outline/outline_color/align keep their old
// meaning; unknown keys and malformed values are ignored — a style
// typo must never kill the title.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "frame.h"

namespace XER::VideoEditor {

struct ClipTextStyle {
    enum class Align { Left, Center, Right };
    enum class VAlign { Top, Middle, Bottom };

    std::string text;           // payload before '|', r-trimmed
    std::string font_family;    // "" -> platform default UI face
    int font_weight = 400;      // DirectWrite range 100..900
    bool italic = false;
    bool underline = false;
    bool strike = false;
    int size_px = 0;            // 0 -> canvas-proportional default
    float letter_spacing = 0.0f;  // tracking: extra px after each glyph
    float line_spacing = 0.0f;    // absolute px line advance; 0 -> auto
    Align align = Align::Left;  // horizontal, inside the box
    VAlign valign = VAlign::Top;  // vertical, inside the box
    float pos_x = 0.0f;         // text inset from box top-left, px
    float pos_y = 0.0f;
    float box_w = 0.0f;         // text box size, px; 0 -> fit the text
    float box_h = 0.0f;
    float corner_radius = 0.0f; // box background rounding, px
    float rotate_deg = 0.0f;    // in-frame rotation about the box center
    float baseline_px = 0.0f;   // extra baseline shift, px (+ = down)
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};   // BGRA fill
    float fill_opacity = 1.0f;  // 0..1 multiplier on the fill alpha
    float outline_px = 0.0f;    // stroke width; >0 replaces auto shadow
    std::array<std::uint8_t, 4> outline_color{0, 0, 0, 255}; // BGRA
    std::array<std::uint8_t, 4> bg_color{0, 0, 0, 0};        // BGRA box bg
    // Shadow. When no shadow_* token is present (`shadow_explicit`
    // false) the renderer keeps the classic look: a soft drop shadow
    // automatically derived from the font size, suppressed while an
    // outline is drawn — i.e. old params render like they always did.
    bool shadow_explicit = false;
    float shadow_x = 0.0f;      // offset, px
    float shadow_y = 0.0f;
    float shadow_blur = 0.0f;   // sigma-ish blur radius, px
    std::array<std::uint8_t, 4> shadow_color{0, 0, 0, 128};  // BGRA

    // ---- karaoke word highlight ------------------------------------
    // Per-word clip-local timing table. word_times[i] maps to the
    // i-th whitespace-delimited word of `text` (newlines count as
    // separators, matching the renderer's own word scan).
    struct WordTime {
        float t0 = 0.0f;   // clip-local seconds
        float t1 = 0.0f;
    };
    bool karaoke = false;
    std::vector<WordTime> word_times;
    std::array<std::uint8_t, 4> karaoke_active{255, 224, 0, 255};  // BGRA
    std::array<std::uint8_t, 4> karaoke_done{0, 0, 0, 0};   // a==0 -> `color`
};

// Parses Clip::source.param for a Text clip (see above). Never fails —
// the result always carries a usable text payload and defaults.
ClipTextStyle ParseClipTextStyle(const std::string& param);

// Inverse of ParseClipTextStyle: emits "text | key=value ..." with
// every field spelled out so a Parse->Serialize->Parse round-trip is
// stable. '|' characters inside the text are stripped (the grammar
// cannot carry them).
std::string SerializeClipTextStyle(const ClipTextStyle& style);

// Rasterizes the style into a BGRA8 STRAIGHT-alpha frame. The frame is
// (re)allocated to the text box size (measured when box w/h are 0),
// padded by the stroke/shadow extents. Uses Skia when the build links
// it (XER_VE_HAS_SKIA); otherwise falls back to the BitmapFont path.
// `clip_local_t` (seconds inside the clip) drives the karaoke word
// highlight — every other field renders time-invariantly, so callers
// without a clock pass 0. Returns false only on allocation/geometry
// failure; an empty text yields a 1x1 transparent frame.
bool RenderClipText(const ClipTextStyle& style, int canvas_h,
                    video::Frame* out, double clip_local_t = 0.0);

}  // namespace XER::VideoEditor
