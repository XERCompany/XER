// TranscriptPanel — see transcript_panel.h for the interaction
// contract. The word flow is computed in Render (MeasureText needs
// a renderer); hit-testing reuses the cached flow. The gutter's
// timestamp marks are real segment starts — clicking one seeks
// through the same map the word click uses.
#include "transcript_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

#include "helper/string_utils.h"
#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/combobox.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;

// MM:SS (H:MM:SS past the hour) — the gutter timestamp format.
std::string FormatStamp(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const long long s = static_cast<long long>(seconds + 0.5);
    char buf[16];
    if (s >= 3600) {
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld",
                      s / 3600, (s / 60) % 60, s % 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld", s / 60,
                      s % 60);
    }
    return buf;
}

std::string Lower(const std::string& text) {
    return XER::Helper::String::ToLowerAscii(text);
}

// Non-prefix match: true when `word` contains `needle` anywhere
// (whisper words may carry punctuation around the token).
bool WordMatches(const std::string& word, const std::string& needle) {
    if (needle.empty()) return false;
    return Lower(word).find(needle) != std::string::npos;
}

}  // namespace

std::shared_ptr<TranscriptPanel> TranscriptPanel::Create() {
    return std::shared_ptr<TranscriptPanel>(new TranscriptPanel());
}

TranscriptPanel::TranscriptPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void TranscriptPanel::SetHooks(Hooks hooks) {
    hooks_ = std::move(hooks);
}

void TranscriptPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    search_input_ = std::make_shared<W::TextInput>("");
    search_input_->SetPlaceholder("Search transcript…");
    search_input_->SetTooltip(
        "Highlight matching words; Enter jumps to the next match");
    search_input_->SetOnValueChanged(
        [this](const std::string& value) { SetSearchText(value); });
    search_input_->SetOnSubmit([this] { StepMatch(1); });

    prev_match_btn_ = std::make_shared<W::Button>(
        "<", W::ButtonStyle::Ghost);
    prev_match_btn_->SetSize(W::ButtonSize::Small);
    prev_match_btn_->SetTooltip("Previous match");
    prev_match_btn_->SetOnClick([this] { StepMatch(-1); });
    next_match_btn_ = std::make_shared<W::Button>(
        ">", W::ButtonStyle::Ghost);
    next_match_btn_->SetSize(W::ButtonSize::Small);
    next_match_btn_->SetTooltip("Next match");
    next_match_btn_->SetOnClick([this] { StepMatch(1); });
    match_count_ = std::make_shared<W::Label>("", true);

    prev_seg_btn_ = std::make_shared<W::Button>(
        "<|", W::ButtonStyle::Ghost);
    prev_seg_btn_->SetSize(W::ButtonSize::Small);
    prev_seg_btn_->SetTooltip("Previous segment");
    prev_seg_btn_->SetOnClick([this] { StepSegment(-1); });
    next_seg_btn_ = std::make_shared<W::Button>(
        "|>", W::ButtonStyle::Ghost);
    next_seg_btn_->SetSize(W::ButtonSize::Small);
    next_seg_btn_->SetTooltip("Next segment");
    next_seg_btn_->SetOnClick([this] { StepSegment(1); });

    caption_style_combo_ = W::Combobox::Create();
    caption_style_combo_->SetOptions(
        {{"Classic", "classic"},
         {"Karaoke", "karaoke"},
         {"Word pop", "wordpop"}});
    caption_style_combo_->SetValue("classic");
    caption_style_combo_->SetPlaceholder("Caption style");
    caption_style_combo_->SetOnSelect(
        [this](const W::Combobox::Option& option) {
            if (hooks_.on_caption_style) {
                hooks_.on_caption_style(
                    option.value.empty() ? option.label
                                         : option.value);
            }
        });

    transcribe_btn_ = std::make_shared<W::Button>(
        "Transcribe", W::ButtonStyle::Primary);
    transcribe_btn_->SetSize(W::ButtonSize::Small);
    transcribe_btn_->SetTooltip(
        "Whisper-transcribe the bound media (async)");
    transcribe_btn_->SetOnClick([this] {
        if (hooks_.on_transcribe) hooks_.on_transcribe();
    });

    cut_btn_ = std::make_shared<W::Button>(
        "Ripple delete", W::ButtonStyle::Destructive);
    cut_btn_->SetSize(W::ButtonSize::Small);
    cut_btn_->SetTooltip(
        "Extract the selected range and close the gap (undoable)");
    cut_btn_->SetOnClick([this] { RippleDeleteSelection(); });
    marker_btn_ = std::make_shared<W::Button>(
        "+ Marker", W::ButtonStyle::Secondary);
    marker_btn_->SetSize(W::ButtonSize::Small);
    marker_btn_->SetTooltip(
        "Add a marker at the selection head named with the text");
    marker_btn_->SetOnClick([this] { AddMarkerForSelection(); });
    captions_btn_ = std::make_shared<W::Button>(
        "Captions", W::ButtonStyle::Secondary);
    captions_btn_->SetSize(W::ButtonSize::Small);
    captions_btn_->SetTooltip(
        "Create styled caption title clips for the selected range");
    captions_btn_->SetOnClick([this] { MakeCaptionsForSelection(); });

    status_ = std::make_shared<W::Label>(
        "Select a media clip to edit by transcript", true);

    AddChild(search_input_);
    AddChild(prev_match_btn_);
    AddChild(next_match_btn_);
    AddChild(match_count_);
    AddChild(prev_seg_btn_);
    AddChild(next_seg_btn_);
    AddChild(caption_style_combo_);
    AddChild(transcribe_btn_);
    AddChild(cut_btn_);
    AddChild(marker_btn_);
    AddChild(captions_btn_);
    AddChild(status_);
    RebuildMatches();
}

// ---- subject + transcript feed -------------------------------------

void TranscriptPanel::SetSubject(std::string media_path,
                                 std::string display_name,
                                 const Clip* clip) {
    const bool same_media = media_path_ == media_path;
    media_path_ = std::move(media_path);
    display_name_ = std::move(display_name);
    has_clip_ = clip != nullptr;
    if (clip != nullptr) clip_ = *clip;
    if (!same_media) {
        // New subject: drop the old transcript view and selection —
        // the shell re-pushes cached segments immediately after.
        words_.clear();
        segments_.clear();
        word_rects_.clear();
        gutter_marks_.clear();
        flow_valid_ = false;
        sel_a_ = sel_b_ = -1;
        hovered_word_ = -1;
        scroll_y_ = 0.0f;
        RebuildMatches();
        if (children_attached_) {
            status_->SetText("Not transcribed — press Transcribe");
        }
    }
    Invalidate();
    RequestLayout();
}

void TranscriptPanel::SetSegments(
    std::vector<Ai::TranscriptSegment> segments) {
    segments_ = std::move(segments);
    SetWords(TranscriptWordsFromSegments(segments_));
}

void TranscriptPanel::SetWords(std::vector<TranscriptWord> words) {
    words_ = std::move(words);
    flow_valid_ = false;
    word_rects_.clear();
    gutter_marks_.clear();
    sel_a_ = sel_b_ = -1;
    hovered_word_ = -1;
    scroll_y_ = 0.0f;
    RebuildMatches();
    if (children_attached_) {
        status_->SetText(
            words_.empty()
                ? "Not transcribed — press Transcribe"
                : std::to_string(words_.size()) + " words · " +
                      std::to_string(segments_.size()) + " segments");
    }
    Invalidate();
    RequestLayout();
}

void TranscriptPanel::SetJobState(bool running, float progress) {
    job_running_ = running;
    job_progress_ = running ? std::clamp(progress, 0.0f, 1.0f) : 0.0f;
    if (children_attached_) {
        transcribe_btn_->SetEnabled(!running);
        transcribe_btn_->SetLoading(running);
    }
    Invalidate();
}

void TranscriptPanel::SetPlayhead(double timeline_seconds) {
    if (timeline_seconds == playhead_) return;
    playhead_ = timeline_seconds;
    // Keep the live word in view while playing — same auto-scroll
    // contract as the legacy panel's follow flag.
    if (follow_playhead_ && !dragging_) {
        const int w = CurrentWord();
        if (w >= 0 && w < static_cast<int>(word_rects_.size())) {
            const Rect r = word_rects_[static_cast<std::size_t>(w)];
            const float margin = r.height * 2.0f;
            const float view_h = TextViewportBottom() - TextAreaTop();
            if (r.y - scroll_y_ < margin ||
                r.y + r.height - scroll_y_ > view_h - margin) {
                scroll_y_ = std::clamp(
                    r.y - margin, 0.0f, MaxScroll());
            }
        }
    }
    Invalidate();
}

int TranscriptPanel::CurrentWord() const {
    return WordAtSourceTime(PlayheadSource());
}

// ---- selection -------------------------------------------------------

bool TranscriptPanel::HasSelection() const {
    return sel_a_ >= 0 && sel_b_ >= 0 && sel_a_ != sel_b_ &&
           sel_a_ < static_cast<int>(words_.size()) &&
           sel_b_ < static_cast<int>(words_.size());
}

void TranscriptPanel::ClearSelection() {
    sel_a_ = sel_b_ = -1;
    Invalidate();
    RequestLayout();   // action bar collapses
}

std::string TranscriptPanel::SelectedText() const {
    if (!HasSelection()) return {};
    const int lo = std::min(sel_a_, sel_b_);
    const int hi = std::max(sel_a_, sel_b_);
    std::string out;
    for (int i = lo; i <= hi; ++i) {
        if (!out.empty()) out += ' ';
        out += words_[static_cast<std::size_t>(i)].text;
    }
    return out;
}

bool TranscriptPanel::SelectionSourceRange(double* t0,
                                           double* t1) const {
    if (!HasSelection()) return false;
    const int lo = std::min(sel_a_, sel_b_);
    const int hi = std::max(sel_a_, sel_b_);
    const double a = words_[static_cast<std::size_t>(lo)].src_t0;
    const double b = words_[static_cast<std::size_t>(hi)].src_t1;
    if (b - a <= 1e-9) return false;
    if (t0 != nullptr) *t0 = a;
    if (t1 != nullptr) *t1 = b;
    return true;
}

bool TranscriptPanel::SelectionTimelineRange(double* t0,
                                             double* t1) const {
    double s0 = 0.0, s1 = 0.0;
    if (!has_clip_ || !SelectionSourceRange(&s0, &s1)) return false;
    return SourceRangeToTimeline(clip_, s0, s1, t0, t1);
}

void TranscriptPanel::SelectWordRange(int a, int b) {
    if (words_.empty()) {
        sel_a_ = sel_b_ = -1;
    } else {
        const int last = static_cast<int>(words_.size()) - 1;
        sel_a_ = std::clamp(a, 0, last);
        sel_b_ = std::clamp(b, 0, last);
    }
    Invalidate();
    RequestLayout();
}

bool TranscriptPanel::ActivateWord(int index) {
    if (index < 0 || index >= static_cast<int>(words_.size())) {
        return false;
    }
    sel_a_ = sel_b_ = -1;          // click = caret, not a range
    follow_playhead_ = true;
    const auto& word = words_[static_cast<std::size_t>(index)];
    const double tl = has_clip_
        ? SourceToTimeline(clip_, word.src_t0)
        : std::numeric_limits<double>::quiet_NaN();
    if (hooks_.on_seek && std::isfinite(tl)) {
        hooks_.on_seek(tl);
    } else if (!has_clip_) {
        SetStatus("media isn't on the timeline — nothing to "
                  "seek to");
    }
    Invalidate();
    RequestLayout();
    return std::isfinite(tl);
}

bool TranscriptPanel::RippleDeleteSelection() {
    double t0 = 0.0, t1 = 0.0;
    if (hooks_.on_ripple_delete == nullptr ||
        !SelectionTimelineRange(&t0, &t1)) {
        return false;
    }
    hooks_.on_ripple_delete(t0, t1);
    return true;
}

bool TranscriptPanel::AddMarkerForSelection() {
    double t0 = 0.0, t1 = 0.0;
    if (hooks_.on_add_marker == nullptr ||
        !SelectionTimelineRange(&t0, &t1)) {
        return false;
    }
    std::string name = SelectedText();
    if (name.size() > 60) name = name.substr(0, 57) + "…";
    hooks_.on_add_marker(t0, name);
    return true;
}

bool TranscriptPanel::MakeCaptionsForSelection() {
    double t0 = 0.0, t1 = 0.0;
    if (hooks_.on_make_captions == nullptr ||
        !SelectionSourceRange(&t0, &t1)) {
        return false;
    }
    hooks_.on_make_captions(t0, t1);
    return true;
}

// ---- search ----------------------------------------------------------

void TranscriptPanel::SetSearchText(std::string text) {
    if (search_input_ != nullptr &&
        search_input_->GetValue() != text) {
        search_input_->SetValue(text);
    }
    if (search_ == text) return;
    search_ = std::move(text);
    RebuildMatches();
}

void TranscriptPanel::RebuildMatches() {
    matches_.clear();
    match_index_ = -1;
    const std::string needle = Lower(search_);
    if (!needle.empty()) {
        for (std::size_t i = 0; i < words_.size(); ++i) {
            if (WordMatches(words_[i].text, needle)) {
                matches_.push_back(static_cast<int>(i));
            }
        }
        if (!matches_.empty()) match_index_ = 0;
    }
    if (match_count_ != nullptr) {
        match_count_->SetText(
            needle.empty()
                ? ""
                : std::to_string(matches_.size()) + " hit(s)");
    }
    Invalidate();
}

bool TranscriptPanel::StepMatch(int direction) {
    if (matches_.empty()) return false;
    const int n = static_cast<int>(matches_.size());
    match_index_ =
        ((match_index_ + direction) % n + n) % n;
    const int word = matches_[static_cast<std::size_t>(match_index_)];
    if (word >= 0 &&
        word < static_cast<int>(word_rects_.size())) {
        ScrollToContentY(
            word_rects_[static_cast<std::size_t>(word)].y, 40.0f);
    }
    if (match_count_ != nullptr) {
        match_count_->SetText(std::to_string(match_index_ + 1) +
                              "/" + std::to_string(n));
    }
    Invalidate();
    return true;
}

// ---- segment navigation --------------------------------------------------

int TranscriptPanel::SegmentAtSourceTime(double src_t) const {
    if (!std::isfinite(src_t)) return -1;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const auto& s = segments_[i];
        if (src_t >= s.t0 && src_t < s.t1) {
            return static_cast<int>(i);
        }
        if (s.t0 > src_t) break;   // sorted — early out
    }
    return -1;
}

bool TranscriptPanel::JumpToSegment(int index) {
    if (index < 0 || index >= static_cast<int>(segments_.size())) {
        return false;
    }
    const double src = segments_[static_cast<std::size_t>(index)].t0;
    const double tl = has_clip_
        ? SourceToTimeline(clip_, src)
        : std::numeric_limits<double>::quiet_NaN();
    if (std::isfinite(tl) && hooks_.on_seek) {
        hooks_.on_seek(tl);
        follow_playhead_ = true;
        return true;
    }
    return false;
}

bool TranscriptPanel::StepSegment(int direction) {
    if (segments_.empty()) return false;
    const double src = PlayheadSource();
    const int current = SegmentAtSourceTime(src);
    int target;
    if (current < 0) {
        // Outside the segments — jump to the nearest edge in the
        // requested direction.
        target = direction > 0 ? 0
                               : static_cast<int>(segments_.size()) - 1;
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            if (direction > 0 && segments_[i].t0 > src) {
                target = static_cast<int>(i);
                break;
            }
            if (direction < 0 && segments_[i].t1 > src) {
                target = static_cast<int>(i);
                break;
            }
        }
    } else {
        target = std::clamp(current + direction, 0,
                            static_cast<int>(segments_.size()) - 1);
    }
    return JumpToSegment(target);
}

// ---- flow / hit-testing ----------------------------------------------------

int TranscriptPanel::WordAtSourceTime(double src_t) const {
    if (!std::isfinite(src_t)) return -1;
    for (std::size_t i = 0; i < words_.size(); ++i) {
        const auto& w = words_[i];
        if (src_t >= w.src_t0 && src_t < w.src_t1) {
            return static_cast<int>(i);
        }
        if (w.src_t0 > src_t) break;   // sorted — early out
    }
    return -1;
}

double TranscriptPanel::PlayheadSource() const {
    if (has_clip_) {
        const double rate =
            clip_.playback_rate > 0.0 ? clip_.playback_rate : 1.0;
        return clip_.in_point +
               (playhead_ - clip_.timeline_start) * rate;
    }
    // Media-bound mode: treat the playhead as media time.
    return playhead_;
}

float TranscriptPanel::TextAreaTop() const {
    const auto bounds = GetBounds();
    // Header (~34) + controls row (~34) + search row (~34).
    return bounds.y + 34.0f + Theme::kControlHeight +
           Theme::kSpaceSm + Theme::kControlHeight +
           Theme::kSpaceXs;
}

float TranscriptPanel::TextViewportBottom() const {
    const auto bounds = GetBounds();
    float bottom = bounds.y + bounds.height - 30.0f;   // status line
    if (HasSelection()) {
        bottom -= Theme::kControlHeight + Theme::kSpaceXs;
    }
    return bottom;
}

float TranscriptPanel::MaxScroll() const {
    return std::max(
        0.0f, flow_height_ - (TextViewportBottom() - TextAreaTop()));
}

void TranscriptPanel::ScrollToContentY(float y, float margin) {
    const float view_h = TextViewportBottom() - TextAreaTop();
    if (y - scroll_y_ < margin ||
        y - scroll_y_ > view_h - margin) {
        scroll_y_ = std::clamp(y - margin, 0.0f, MaxScroll());
    }
}

void TranscriptPanel::EnsureFlow(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float pad = tokens.spacing_medium;
    text_x_ = bounds.x + pad + gutter_w_;
    const float width =
        std::max(40.0f, bounds.x + bounds.width - pad - text_x_);
    if (flow_valid_ && width == flow_width_) return;
    flow_valid_ = true;
    flow_width_ = width;
    word_rects_.assign(words_.size(), Rect{});
    gutter_marks_.clear();

    const XER::UI::Rendering::TextStyle word_style{
        tokens.foreground, tokens.font_body, 400};
    const float space_w =
        std::max(2.0f, renderer.MeasureText(" ", word_style).width);
    const float line_h = tokens.font_body * 1.7f;
    const float right = text_x_ + width;
    float x = text_x_;
    float y = 0.0f;   // content space (relative to TextAreaTop)
    int segment_index = -1;
    for (std::size_t i = 0; i < words_.size(); ++i) {
        const auto& w = words_[i];
        if (w.segment_start) {
            if (x > text_x_) {
                x = text_x_;
                y += line_h;
            }
            gutter_marks_.push_back(
                {y, FormatStamp(w.src_t0), static_cast<int>(i),
                 ++segment_index});
        }
        const float wpx =
            renderer.MeasureText(w.text, word_style).width;
        if (x + wpx > right && x > text_x_) {
            x = text_x_;
            y += line_h;
        }
        word_rects_[i] = {x, y, std::max(1.0f, wpx), line_h};
        x += wpx + space_w;
    }
    flow_height_ = words_.empty() ? 0.0f : y + line_h;
}

int TranscriptPanel::WordAt(const Point& position) const {
    if (!flow_valid_ || words_.empty()) return -1;
    const float top = TextAreaTop();
    if (position.y < top || position.y >= TextViewportBottom() ||
        position.x < text_x_ - gutter_w_) {
        return -1;
    }
    const float content_y = position.y - top + scroll_y_;
    for (std::size_t i = 0; i < word_rects_.size(); ++i) {
        const Rect& r = word_rects_[i];
        if (content_y >= r.y && content_y < r.y + r.height &&
            position.x >= r.x - 2.0f &&
            position.x < r.x + r.width + 2.0f) {
            return static_cast<int>(i);
        }
        if (r.y > content_y) break;   // rects are y-sorted
    }
    return -1;
}

// ---- widget contract ---------------------------------------------------------

void TranscriptPanel::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.panel, 0.0f);

    const float pad = tokens.spacing_medium;
    VeIcons::DrawSectionHeader(
        renderer, tokens,
        {bounds.x + pad, bounds.y + pad * 0.6f,
         bounds.width - pad * 2.0f, Theme::kRowHeight},
        display_name_.empty() ? "Transcript"
                              : "Transcript — " + display_name_,
        false);

    // Job progress line under the controls rows.
    if (job_running_) {
        const float bar_w = bounds.width - pad * 2.0f;
        const float bar_y = TextAreaTop() - 14.0f;
        renderer.FillRect({bounds.x + pad, bar_y, bar_w, 4.0f},
                          tokens.secondary, 2.0f);
        renderer.FillRect(
            {bounds.x + pad, bar_y, bar_w * job_progress_, 4.0f},
            tokens.accent, 2.0f);
    }

    const float top = TextAreaTop();
    const float bottom = TextViewportBottom();
    if (words_.empty()) {
        const char* hint =
            media_path_.empty()
                ? "select a media clip — or right-click it and choose "
                  "\"Edit by Transcript\""
                : (job_running_ ? "transcribing…"
                                : "press Transcribe to build the "
                                  "editable transcript");
        renderer.DrawText(hint,
                          {bounds.x + 12.0f, top + 8.0f,
                           bounds.width - 24.0f, 20.0f},
                          {tokens.muted, tokens.font_small, 400});
        return;
    }

    EnsureFlow(renderer, tokens);
    scroll_y_ = std::clamp(scroll_y_, 0.0f, MaxScroll());
    const int current = CurrentWord();
    const int sel_lo =
        HasSelection() ? std::min(sel_a_, sel_b_) : -1;
    const int sel_hi =
        HasSelection() ? std::max(sel_a_, sel_b_) : -1;
    const int match_word =
        match_index_ >= 0 &&
                match_index_ < static_cast<int>(matches_.size())
            ? matches_[static_cast<std::size_t>(match_index_)]
            : -1;

    renderer.PushClip({bounds.x, top, bounds.width, bottom - top});
    const XER::UI::Rendering::TextStyle word_style{
        tokens.foreground, tokens.font_body, 400};
    const XER::UI::Rendering::TextStyle outside_style{
        tokens.muted, tokens.font_body, 400};
    const XER::UI::Rendering::TextStyle gutter_style{
        tokens.muted, tokens.font_small, 400};
    for (std::size_t i = 0; i < words_.size(); ++i) {
        const Rect& r = word_rects_[i];
        const float ry = top + r.y - scroll_y_;
        if (ry + r.height < top || ry > bottom) continue;
        const int idx = static_cast<int>(i);
        const Rect draw{r.x, ry, r.width, r.height};
        if (sel_lo >= 0 && idx >= sel_lo && idx <= sel_hi) {
            renderer.FillRect({r.x - 1.0f, ry + 1.0f,
                               r.width + 2.0f, r.height - 2.0f},
                              tokens.selection,
                              tokens.radius_small);
        } else if (idx == current) {
            renderer.FillRect({r.x - 1.0f, ry + 1.0f,
                               r.width + 2.0f, r.height - 2.0f},
                              tokens.accent, tokens.radius_small);
        } else if (idx == hovered_word_) {
            renderer.FillRect({r.x - 1.0f, ry + 1.0f,
                               r.width + 2.0f, r.height - 2.0f},
                              tokens.hover, tokens.radius_small);
        }
        // Search hit wash under the text (amber; the current match
        // gets the stronger wash).
        if (!search_.empty() &&
            WordMatches(words_[i].text, Lower(search_))) {
            const bool is_current = idx == match_word;
            renderer.FillRect(
                {r.x - 1.0f, ry + 1.0f, r.width + 2.0f,
                 r.height - 2.0f},
                Theme::WithAlpha(Theme::kWarningChip,
                                 is_current ? 0.55f : 0.28f),
                tokens.radius_small);
        }
        // Words outside the bound clip's consumed source span draw
        // muted — they have no timeline position.
        const bool mappable =
            !has_clip_ ||
            std::isfinite(SourceToTimeline(clip_, words_[i].src_t0));
        renderer.DrawText(words_[i].text, draw,
                          mappable ? word_style : outside_style);
    }
    for (const GutterMark& mark : gutter_marks_) {
        const float ry = top + mark.y - scroll_y_;
        if (ry < top - 20.0f || ry > bottom) continue;
        renderer.DrawText(mark.stamp,
                          {bounds.x + 4.0f, ry + 2.0f,
                           gutter_w_ - 6.0f, 16.0f},
                          gutter_style);
    }
    renderer.PopClip();

    // Scrollbar affordance on the right edge.
    if (flow_height_ > bottom - top) {
        const float track_h = bottom - top;
        const float thumb = std::max(
            18.0f, track_h * (bottom - top) / flow_height_);
        const float ty =
            top + (track_h - thumb) *
                      (MaxScroll() > 0.0f
                           ? scroll_y_ / MaxScroll()
                           : 0.0f);
        renderer.FillRect(
            {bounds.x + bounds.width - 4.0f, ty, 3.0f, thumb},
            tokens.border, 1.5f);
    }
}

void TranscriptPanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;

    // Controls row under the drawn header: caption style + segment
    // nav on the left, Transcribe on the right.
    const float ctrl_y = bounds.y + 34.0f;
    const float btn_w = 96.0f;
    transcribe_btn_->Layout(
        {bounds.x + bounds.width - pad - btn_w, ctrl_y, btn_w,
         Theme::kControlHeight},
        tokens);
    const bool ready =
        hooks_.transcribe_ready == nullptr || hooks_.transcribe_ready();
    transcribe_btn_->SetEnabled(ready && !job_running_);
    transcribe_btn_->SetText(
        words_.empty() ? "Transcribe" : "Re-transcribe");
    transcribe_btn_->SetTooltip(
        ready ? "Whisper-transcribe the bound media (async)"
              : "Whisper unavailable — install libwhisper + a ggml "
                "model (models/ or XER_WHISPER_MODEL)");
    transcribe_btn_->SetVisible(!media_path_.empty());

    const float combo_w = 128.0f;
    caption_style_combo_->Layout(
        {bounds.x + pad, ctrl_y, combo_w, Theme::kControlHeight},
        tokens);
    caption_style_combo_->SetVisible(!media_path_.empty());
    const float seg_w = 30.0f;
    prev_seg_btn_->Layout(
        {bounds.x + pad + combo_w + Theme::kSpaceSm, ctrl_y, seg_w,
         Theme::kControlHeight},
        tokens);
    next_seg_btn_->Layout(
        {bounds.x + pad + combo_w + Theme::kSpaceSm + seg_w + 2.0f,
         ctrl_y, seg_w, Theme::kControlHeight},
        tokens);
    prev_seg_btn_->SetVisible(!segments_.empty());
    next_seg_btn_->SetVisible(!segments_.empty());

    // Search row: input (flex) | "n/m" | [<] [>].
    const float search_y =
        ctrl_y + Theme::kControlHeight + Theme::kSpaceXs;
    const float nav_w = 30.0f;
    const float count_w = 52.0f;
    const float tail_w = count_w + nav_w * 2.0f + Theme::kSpaceXs;
    search_input_->Layout(
        {bounds.x + pad, search_y,
         std::max(40.0f, bounds.width - pad * 2.0f - tail_w -
                             Theme::kSpaceXs),
         Theme::kControlHeight},
        tokens);
    const float tail_x = bounds.x + bounds.width - pad - tail_w;
    match_count_->Layout(
        {tail_x, search_y + 6.0f, count_w, 18.0f}, tokens);
    prev_match_btn_->Layout(
        {tail_x + count_w + Theme::kSpaceXs, search_y, nav_w,
         Theme::kControlHeight},
        tokens);
    next_match_btn_->Layout(
        {tail_x + count_w + Theme::kSpaceXs + nav_w, search_y, nav_w,
         Theme::kControlHeight},
        tokens);
    const bool searching = !matches_.empty();
    prev_match_btn_->SetEnabled(searching);
    next_match_btn_->SetEnabled(searching);

    // Bottom action bar — only while a word range is selected.
    const bool sel = HasSelection();
    const float bar_y =
        bounds.y + bounds.height - 30.0f -
        (sel ? Theme::kControlHeight + Theme::kSpaceXs : 0.0f);
    if (sel) {
        float ax = bounds.x + pad;
        cut_btn_->Layout({ax, bar_y, 96.0f, Theme::kControlHeight},
                         tokens);
        ax += 100.0f;
        marker_btn_->Layout({ax, bar_y, 76.0f,
                             Theme::kControlHeight},
                            tokens);
        ax += 80.0f;
        captions_btn_->Layout({ax, bar_y, 76.0f,
                               Theme::kControlHeight},
                              tokens);
        const bool mapped = has_clip_;
        cut_btn_->SetEnabled(mapped);
        marker_btn_->SetEnabled(mapped);
        captions_btn_->SetEnabled(mapped);
    }
    cut_btn_->SetVisible(sel);
    marker_btn_->SetVisible(sel);
    captions_btn_->SetVisible(sel);

    status_->Layout({bounds.x + pad,
                     bounds.y + bounds.height - 26.0f,
                     bounds.width - pad * 2.0f, 20.0f},
                    tokens);
    Invalidate();
}

bool TranscriptPanel::HandleEvent(const W::Event& event) {
    switch (event.type) {
        case W::Event::Type::MouseWheel: {
            const float range = MaxScroll();
            if (range <= 0.0f) return false;
            scroll_y_ = std::clamp(
                scroll_y_ -
                    static_cast<float>(event.wheel_delta) * 0.5f,
                0.0f, range);
            follow_playhead_ = false;   // user scroll wins until a seek
            Invalidate();
            return true;
        }
        case W::Event::Type::MouseMove: {
            const int w = WordAt(event.position);
            if (dragging_ && w >= 0 && w != sel_b_) {
                sel_b_ = w;
                Invalidate();
                RequestLayout();
            }
            if (w != hovered_word_) {
                hovered_word_ = w;
                Invalidate();
            }
            return dragging_;
        }
        case W::Event::Type::MouseDown: {
            if (event.mouse_button != W::MouseButton::Left) {
                return false;
            }
            // Gutter stamp hit → segment seek (x in the gutter
            // column, y inside the text viewport).
            const float top = TextAreaTop();
            const float content_y =
                event.position.y - top + scroll_y_;
            if (event.position.x < text_x_ &&
                event.position.y >= top &&
                event.position.y < TextViewportBottom()) {
                for (const GutterMark& mark : gutter_marks_) {
                    if (content_y >= mark.y &&
                        content_y < mark.y + 22.0f &&
                        mark.segment_index >= 0) {
                        JumpToSegment(mark.segment_index);
                        return true;
                    }
                }
            }
            const int w = WordAt(event.position);
            if (w < 0) {
                if (HasSelection()) {
                    ClearSelection();
                    return true;
                }
                return false;
            }
            dragging_ = true;
            suppress_click_ = false;
            sel_a_ = sel_b_ = w;
            Invalidate();
            return true;
        }
        case W::Event::Type::MouseUp: {
            if (!dragging_) return false;
            dragging_ = false;
            if (sel_a_ != sel_b_) {
                // A real range-drag: swallow the Click the tree emits
                // next so it doesn't seek+clear the selection.
                suppress_click_ = true;
                RequestLayout();   // action bar appears
            }
            Invalidate();
            return true;
        }
        case W::Event::Type::Click: {
            if (suppress_click_) {
                suppress_click_ = false;
                return true;
            }
            const int w = WordAt(event.position);
            if (w < 0) return false;
            ActivateWord(w);
            return true;
        }
        case W::Event::Type::KeyDown: {
            if (event.key == W::KeyCode::Escape &&
                (HasSelection() || hovered_word_ >= 0)) {
                ClearSelection();
                hovered_word_ = -1;
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

void TranscriptPanel::SetStatus(const std::string& text,
                                bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void TranscriptPanel::SyncToolbar() {}

void TranscriptPanel::Tick(double /*dt_seconds*/) {}

}  // namespace XER::VideoEditor::Ui2
