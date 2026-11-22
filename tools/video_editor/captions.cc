// captions.cc — see captions.h for the preset contracts.
#include "captions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace XER::VideoEditor {
namespace {

// Minimum caption clip length — mirrors the 0.4s floor the original
// captionist path applied (word-pop clips keep a shorter floor so
// rapid words still pop).
constexpr double kMinCaptionDuration = 0.4;
constexpr double kMinPopDuration = 0.25;

// Splits the flat word stream into per-segment groups (segment_start
// marks the first word of each whisper segment).
std::vector<std::vector<const TranscriptWord*>> GroupBySegment(
    const std::vector<TranscriptWord>& words) {
    std::vector<std::vector<const TranscriptWord*>> groups;
    for (const TranscriptWord& w : words) {
        if (w.segment_start || groups.empty()) {
            groups.emplace_back();
        }
        groups.back().push_back(&w);
    }
    return groups;
}

std::string JoinWords(
    const std::vector<const TranscriptWord*>& words) {
    std::string out;
    for (const TranscriptWord* w : words) {
        if (!out.empty()) out += ' ';
        out += w->text;
    }
    return out;
}

// Shared text style for all presets — karaoke adds its fields on top
// of this base.
ClipTextStyle BaseStyle(const std::string& text,
                        const CaptionStyle& style) {
    ClipTextStyle s;
    s.text = text;
    s.size_px = style.size_px;
    s.align = ClipTextStyle::Align::Center;
    s.valign = style.valign;
    s.box_w = static_cast<float>(std::max(0, style.box_w_px));
    s.color = style.color;
    s.outline_px = std::max(0.0f, style.outline_px);
    s.outline_color = style.outline_color;
    return s;
}

Clip MakeClip(int track_index, double start, double duration,
              const std::string& param) {
    Clip clip;
    clip.id = NewClipId();
    clip.name = "Caption";
    clip.source.kind = SourceKind::Text;
    clip.track = track_index;
    clip.timeline_start = start;
    clip.duration = duration;
    clip.source.param = param;
    return clip;
}

// Opacity fade-in used by the classic + karaoke presets.
void AddFadeIn(Clip* clip) {
    std::vector<Keyframe> keys;
    keys.push_back({0.0, 0.0f, KeyEase::EaseOut});
    keys.push_back({std::min(0.15, clip->duration), 1.0f,
                    KeyEase::Hold});
    clip->keyframes[KeyProp::Opacity] = std::move(keys);
}

// WordPop's signature move: scale 0.6 -> 1.0 with the Back overshoot,
// plus a fast opacity fade so the word never hard-cuts in.
void AddPopIn(Clip* clip) {
    const double pop_end = std::min(0.16, clip->duration);
    std::vector<Keyframe> scale_keys;
    scale_keys.push_back({0.0, 0.6f, KeyEase::Back});
    scale_keys.push_back({pop_end, 1.0f, KeyEase::Hold});
    clip->keyframes[KeyProp::Scale] = std::move(scale_keys);
    std::vector<Keyframe> opacity_keys;
    opacity_keys.push_back({0.0, 0.0f, KeyEase::Linear});
    opacity_keys.push_back({std::min(0.06, clip->duration), 1.0f,
                            KeyEase::Hold});
    clip->keyframes[KeyProp::Opacity] = std::move(opacity_keys);
}

// Clip-local seconds for a source time, through the bound clip's
// mapping. Times outside the consumed source span clamp to its edges
// (like SourceRangeToTimeline), so a trimmed word lights at the
// boundary instead of staying permanently inactive.
double LocalTime(const Clip& bound, double clip_tl_start,
                 double src_t) {
    const double rate =
        bound.playback_rate > 0.0 ? bound.playback_rate : 1.0;
    const double lo = bound.in_point;
    const double hi = lo + bound.duration * rate;
    const double clamped = std::clamp(src_t, lo, hi);
    return bound.timeline_start + (clamped - lo) / rate -
           clip_tl_start;
}

}  // namespace

std::vector<Clip> BuildCaptionClips(
    const std::vector<TranscriptWord>& words, const Clip& bound,
    double src_t0, double src_t1, int track_index,
    const CaptionStyle& style) {
    std::vector<Clip> out;
    if (src_t1 <= src_t0) return out;
    for (const auto& group : GroupBySegment(words)) {
        if (group.empty()) continue;
        const double seg_t0 = group.front()->src_t0;
        const double seg_t1 = group.back()->src_t1;
        if (seg_t1 <= src_t0 || seg_t0 >= src_t1) continue;
        double tl0 = 0.0, tl1 = 0.0;
        if (!SourceRangeToTimeline(bound, seg_t0, seg_t1, &tl0, &tl1)) {
            continue;
        }

        if (style.preset == CaptionStyle::Preset::WordPop) {
            // Chunk the group's words; each chunk spans from its first
            // word's start to the next chunk's start (or the group's
            // mapped end).
            const int per = std::clamp(style.words_per_clip, 1, 8);
            for (std::size_t c = 0; c < group.size();) {
                const std::size_t end =
                    std::min(group.size(), c + static_cast<std::size_t>(per));
                std::vector<const TranscriptWord*> chunk(
                    group.begin() + static_cast<std::ptrdiff_t>(c),
                    group.begin() + static_cast<std::ptrdiff_t>(end));
                const double chunk_end_src =
                    end < group.size() ? group[end]->src_t0
                                       : group.back()->src_t1;
                double ct0 = 0.0, ct1 = 0.0;
                if (SourceRangeToTimeline(bound, chunk.front()->src_t0,
                                          chunk_end_src, &ct0, &ct1)) {
                    ClipTextStyle s =
                        BaseStyle(JoinWords(chunk), style);
                    s.color = style.active_color;
                    Clip clip = MakeClip(
                        track_index, ct0,
                        std::max(kMinPopDuration, ct1 - ct0),
                        SerializeClipTextStyle(s));
                    if (style.animate) AddPopIn(&clip);
                    out.push_back(std::move(clip));
                }
                c = end;
            }
            continue;
        }

        ClipTextStyle s = BaseStyle(JoinWords(group), style);
        const double duration = std::max(kMinCaptionDuration,
                                         tl1 - tl0);
        if (style.preset == CaptionStyle::Preset::Karaoke) {
            s.karaoke = true;
            s.karaoke_active = style.active_color;
            s.karaoke_done = style.done_color;
            s.word_times.reserve(group.size());
            for (const TranscriptWord* w : group) {
                ClipTextStyle::WordTime wt;
                wt.t0 = static_cast<float>(
                    std::max(0.0, LocalTime(bound, tl0, w->src_t0)));
                wt.t1 = static_cast<float>(
                    std::max(0.0, LocalTime(bound, tl0, w->src_t1)));
                if (wt.t1 < wt.t0) wt.t1 = wt.t0;
                s.word_times.push_back(wt);
            }
        }
        Clip clip = MakeClip(track_index, tl0, duration,
                             SerializeClipTextStyle(s));
        if (style.animate) AddFadeIn(&clip);
        out.push_back(std::move(clip));
    }
    return out;
}

const char* CaptionPresetName(CaptionStyle::Preset preset) {
    switch (preset) {
        case CaptionStyle::Preset::Karaoke: return "karaoke";
        case CaptionStyle::Preset::WordPop: return "wordpop";
        case CaptionStyle::Preset::Classic:
        default:                           return "classic";
    }
}

CaptionStyle::Preset CaptionPresetByName(const std::string& name) {
    if (name == "karaoke") return CaptionStyle::Preset::Karaoke;
    if (name == "wordpop" || name == "word_pop" || name == "pop") {
        return CaptionStyle::Preset::WordPop;
    }
    return CaptionStyle::Preset::Classic;
}

}  // namespace XER::VideoEditor
