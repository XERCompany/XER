// Animated caption clip generation — the CapCut/Premiere "animated
// captions" feature. Consumes the transcript's per-word source-time
// spans (transcript_edit.h — real whisper token timings when the
// probed DLL exports them, proportional spans otherwise) and emits
// AddClip-ready Text clips.
//
//   Classic  — one clip per whisper segment, flat color (the
//              original captionist recipe).
//   Karaoke  — one clip per segment whose per-word highlight is drawn
//              by the clip itself: the word span table rides the
//              clip's source.param (kwt=, kact=, kdone= — see
//              text_style.h) and the renderer colors the word under
//              clip-local time. No overlay alignment math needed.
//   WordPop  — one clip per word chunk with a scale-pop + fade-in
//              keyframe animation (single-word-at-a-time captions).
//
// Headless like transcript_edit.*: pure Clip construction — the app
// adds each clip through the history model (validation + undo) and
// the smoke checks exercise the builder directly.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "text_style.h"
#include "timeline_model.h"
#include "transcript_edit.h"

namespace XER::VideoEditor {

struct CaptionStyle {
    enum class Preset { Classic, Karaoke, WordPop };
    Preset preset = Preset::Classic;

    int size_px = 56;
    int box_w_px = 1600;                 // text box width token (w=)
    ClipTextStyle::VAlign valign =
        ClipTextStyle::VAlign::Bottom;
    // BGRA colors matching ClipTextStyle/ParseMatteColor convention.
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};      // inactive
    std::array<std::uint8_t, 4> active_color{255, 224, 0, 255}; // highlight/pop
    std::array<std::uint8_t, 4> done_color{0, 0, 0, 0};  // a=0 -> `color`
    std::array<std::uint8_t, 4> outline_color{0, 0, 0, 255};
    float outline_px = 2.0f;

    // WordPop: words packed per clip — 1 = each word is its own clip,
    // 2-3 gives the "two/three words at a time" subtitle style.
    int words_per_clip = 1;
    // Karaoke/Classic get an opacity fade-in; WordPop a scale pop +
    // fade-in. False produces bare static clips.
    bool animate = true;
};

// Builds caption clips for the words intersecting SOURCE range
// [src_t0, src_t1) — the same selection contract the transcript
// panel's "Captions" action passes. `bound` is the media clip the
// transcript belongs to (its in_point/rate maps source<->timeline);
// `track_index` is stamped on every clip. Returned clips carry fresh
// ids and are AddClip-ready — the caller validates + adds each.
std::vector<Clip> BuildCaptionClips(
    const std::vector<TranscriptWord>& words, const Clip& bound,
    double src_t0, double src_t1, int track_index,
    const CaptionStyle& style);

// "classic" | "karaoke" | "wordpop" — the UI/extension wire names.
const char* CaptionPresetName(CaptionStyle::Preset preset);
CaptionStyle::Preset CaptionPresetByName(const std::string& name);

}  // namespace XER::VideoEditor
