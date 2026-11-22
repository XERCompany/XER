// Transcript dock panel — see transcript_panel.h for the interaction
// contract. The word flow is computed in Render (MeasureText needs a
// renderer); hit-testing reuses the cached flow.
#include "transcript_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/combobox.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;

// MM:SS (H:MM:SS past the hour) — the gutter timestamp format.
std::string FormatStamp(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const long long s = static_cast<long long>(seconds + 0.5);
    char buf[16];
    if (s >= 3600) {
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", s / 3600,
                      (s / 60) % 60, s % 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld", s / 60,
                      s % 60);
    }
    return buf;
}

}  // namespace

TranscriptPanelWidget::TranscriptPanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);
}

void TranscriptPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    transcribe_btn_ = std::make_shared<Widgets::Button>(
        "Transcribe", Widgets::ButtonStyle::Secondary);
    transcribe_btn_->SetTooltip(
        "Whisper-transcribe the bound media (async)");
    transcribe_btn_->SetOnClick([this] {
        if (hooks_.on_transcribe) hooks_.on_transcribe();
    });

    cut_btn_ = std::make_shared<Widgets::Button>(
        "Ripple delete", Widgets::ButtonStyle::Destructive);
    cut_btn_->SetTooltip(
        "Extract the selected range and close the gap (undoable)");
    cut_btn_->SetOnClick([this] {
        double t0 = 0.0, t1 = 0.0;
        if (hooks_.on_ripple_delete &&
            SelectionTimelineRange(&t0, &t1)) {
            hooks_.on_ripple_delete(t0, t1);
        }
    });

    marker_btn_ = std::make_shared<Widgets::Button>(
        "+ Marker", Widgets::ButtonStyle::Secondary);
    marker_btn_->SetTooltip(
        "Add a marker at the selection head named with the text");
    marker_btn_->SetOnClick([this] {
        double t0 = 0.0, t1 = 0.0;
        if (hooks_.on_add_marker &&
            SelectionTimelineRange(&t0, &t1)) {
            std::string name = SelectedText();
            if (name.size() > 60) name = name.substr(0, 57) + "…";
            hooks_.on_add_marker(t0, name);
        }
    });

    captions_btn_ = std::make_shared<Widgets::Button>(
        "Captions", Widgets::ButtonStyle::Secondary);
    captions_btn_->SetTooltip(
        "Create styled caption title clips for the selected range");
    captions_btn_->SetOnClick([this] {
        double t0 = 0.0, t1 = 0.0;
        if (hooks_.on_make_captions &&
            SelectionSourceRange(&t0, &t1)) {
            hooks_.on_make_captions(t0, t1);
        }
    });

    // Animated-caption style picker — sits in the header controls
    // row; the app-side CaptionStyle tracks the picked preset.
    caption_style_combo_ = Widgets::Combobox::Create();
    caption_style_combo_->SetOptions(
        {{"Classic", "classic"},
         {"Karaoke", "karaoke"},
         {"Word pop", "wordpop"}});
    caption_style_combo_->SetValue("classic");
    caption_style_combo_->SetPlaceholder("Caption style");
    caption_style_combo_->SetOnSelect(
        [this](const Widgets::Combobox::Option& option) {
            if (hooks_.on_caption_style) {
                hooks_.on_caption_style(
                    option.value.empty() ? option.label
                                         : option.value);
            }
        });

    status_ = std::make_shared<Widgets::Label>(
        "Select a media clip to edit by transcript", true);

    AddChild(transcribe_btn_);
    AddChild(caption_style_combo_);
    AddChild(cut_btn_);
    AddChild(marker_btn_);
    AddChild(captions_btn_);
    AddChild(status_);
}

void TranscriptPanelWidget::SetSubject(std::string media_path,
                                       std::string display_name,
                                       const Clip* clip) {
    const bool same_media = media_path_ == media_path;
    media_path_ = std::move(media_path);
    display_name_ = std::move(display_name);
    has_clip_ = clip != nullptr;
    if (clip != nullptr) clip_ = *clip;
    if (!same_media) {
        // New subject: drop the old transcript view and selection —
        // the app re-pushes cached words immediately after.
        words_.clear();
        word_rects_.clear();
        gutter_marks_.clear();
        flow_valid_ = false;
        sel_a_ = sel_b_ = -1;
        hovered_word_ = -1;
        scroll_y_ = 0.0f;
        if (children_attached_) {
            status_->SetText("Not transcribed — press Transcribe");
        }
    }
    Invalidate();
    RequestLayout();
}

void TranscriptPanelWidget::SetWords(
    std::vector<TranscriptWord> words) {
    words_ = std::move(words);
    flow_valid_ = false;
    word_rects_.clear();
    gutter_marks_.clear();
    sel_a_ = sel_b_ = -1;
    hovered_word_ = -1;
    scroll_y_ = 0.0f;
    if (children_attached_) {
        status_->SetText(
            words_.empty()
                ? "Not transcribed — press Transcribe"
                : std::to_string(words_.size()) + " words");
    }
    Invalidate();
    RequestLayout();
}

void TranscriptPanelWidget::SetJobState(bool running, float progress) {
    job_running_ = running;
    job_progress_ = running ? std::clamp(progress, 0.0f, 1.0f) : 0.0f;
    if (children_attached_) {
        transcribe_btn_->SetEnabled(!running);
    }
    Invalidate();
}

void TranscriptPanelWidget::SetPlayhead(double timeline_seconds) {
    if (timeline_seconds == playhead_) return;
    playhead_ = timeline_seconds;
    // Keep the live word in view while playing — same auto-scroll
    // contract as the marker proximity highlight.
    if (follow_playhead_ && !dragging_) {
        const int w = CurrentWord();
        if (w >= 0 && w < static_cast<int>(word_rects_.size())) {
            const Rect r = word_rects_[static_cast<std::size_t>(w)];
            const float top = TextAreaTop();
            const float bottom = TextViewportBottom();
            const float ry = top + r.y - scroll_y_;
            const float margin = r.height * 2.0f;
            if (ry < top + margin) {
                scroll_y_ = std::max(0.0f, r.y - margin);
            } else if (ry + r.height > bottom - margin) {
                scroll_y_ = std::min(
                    MaxScroll(), r.y + r.height -
                                     (bottom - top) + margin);
            }
        }
    }
    Invalidate();
}

bool TranscriptPanelWidget::HasSelection() const {
    return sel_a_ >= 0 && sel_b_ >= 0 && sel_a_ != sel_b_ &&
           sel_a_ < static_cast<int>(words_.size()) &&
           sel_b_ < static_cast<int>(words_.size());
}

void TranscriptPanelWidget::ClearSelection() {
    sel_a_ = sel_b_ = -1;
    Invalidate();
    RequestLayout();   // action bar collapses
}

std::string TranscriptPanelWidget::SelectedText() const {
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

bool TranscriptPanelWidget::SelectionSourceRange(double* t0,
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

bool TranscriptPanelWidget::SelectionTimelineRange(double* t0,
                                                   double* t1) const {
    double s0 = 0.0, s1 = 0.0;
    if (!has_clip_ || !SelectionSourceRange(&s0, &s1)) return false;
    return SourceRangeToTimeline(clip_, s0, s1, t0, t1);
}

double TranscriptPanelWidget::PlayheadSource() const {
    if (has_clip_) {
        const double rate =
            clip_.playback_rate > 0.0 ? clip_.playback_rate : 1.0;
        return clip_.in_point + (playhead_ - clip_.timeline_start) * rate;
    }
    // Media-bound mode: treat the playhead as media time — the
    // highlight still tracks while a source-monitor preview plays.
    return playhead_;
}

int TranscriptPanelWidget::WordAtSourceTime(double src_t) const {
    if (!std::isfinite(src_t)) return -1;
    // Linear scan is fine — visible transcripts run to a few thousand
    // words and this fires once per paint, not per word.
    for (std::size_t i = 0; i < words_.size(); ++i) {
        const auto& w = words_[i];
        if (src_t >= w.src_t0 && src_t < w.src_t1) {
            return static_cast<int>(i);
        }
        if (w.src_t0 > src_t) break;   // sorted — early out
    }
    return -1;
}

int TranscriptPanelWidget::CurrentWord() const {
    return WordAtSourceTime(PlayheadSource());
}

float TranscriptPanelWidget::TextAreaTop() const {
    const auto bounds = GetBounds();
    // Header (~34) + controls row (~34) + gap.
    return bounds.y + 34.0f + VeTheme::kControlHeight +
           VeTheme::kSpaceSm;
}

float TranscriptPanelWidget::TextViewportBottom() const {
    const auto bounds = GetBounds();
    float bottom = bounds.y + bounds.height - 30.0f;   // status line
    if (HasSelection()) bottom -= VeTheme::kControlHeight +
                                     VeTheme::kSpaceXs;
    return bottom;
}

float TranscriptPanelWidget::MaxScroll() const {
    return std::max(
        0.0f, flow_height_ - (TextViewportBottom() - TextAreaTop()));
}

void TranscriptPanelWidget::EnsureFlow(
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
    for (std::size_t i = 0; i < words_.size(); ++i) {
        const auto& w = words_[i];
        if (w.segment_start && x > text_x_) {
            x = text_x_;
            y += line_h;
        }
        if (w.segment_start) {
            gutter_marks_.emplace_back(y, FormatStamp(w.src_t0));
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

int TranscriptPanelWidget::WordAt(
    const XER::UI::Layout::Point& position) const {
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
            position.x >= r.x - 2.0f && position.x < r.x + r.width + 2.0f) {
            return static_cast<int>(i);
        }
        if (r.y > content_y) break;   // rects are y-sorted
    }
    return -1;
}

void TranscriptPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);

    const float pad = tokens.spacing_medium;
    VeIcons::DrawSectionHeader(
        renderer, tokens,
        {bounds.x + pad, bounds.y + pad * 0.6f,
         bounds.width - pad * 2.0f, VeTheme::kRowHeight},
        display_name_.empty() ? "Transcript"
                              : "Transcript — " + display_name_,
        false);

    // Job progress line under the controls row.
    if (job_running_) {
        const float bar_w = bounds.width - pad * 2.0f;
        const float bar_y = TextAreaTop() - 18.0f;
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
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        return;
    }

    EnsureFlow(renderer, tokens);
    scroll_y_ = std::clamp(scroll_y_, 0.0f, MaxScroll());
    const int current = CurrentWord();
    const int sel_lo =
        HasSelection() ? std::min(sel_a_, sel_b_) : -1;
    const int sel_hi =
        HasSelection() ? std::max(sel_a_, sel_b_) : -1;

    // The text viewport clips manually — the widget's own clip rect
    // covers the header/action bars too, so the word draw must stay
    // inside [top, bottom).
    renderer.PushClip({bounds.x, top, bounds.width,
                       bottom - top});
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
            renderer.FillRect({r.x - 1.0f, ry + 1.0f, r.width + 2.0f,
                               r.height - 2.0f},
                              tokens.selection, tokens.radius_small);
        } else if (idx == current) {
            renderer.FillRect({r.x - 1.0f, ry + 1.0f, r.width + 2.0f,
                               r.height - 2.0f},
                              tokens.accent, tokens.radius_small);
        } else if (idx == hovered_word_) {
            renderer.FillRect({r.x - 1.0f, ry + 1.0f, r.width + 2.0f,
                               r.height - 2.0f},
                              tokens.hover, tokens.radius_small);
        }
        // Words outside the bound clip's consumed source span draw
        // muted — they have no timeline position.
        const bool mappable =
            !has_clip_ ||
            std::isfinite(SourceToTimeline(clip_, words_[i].src_t0));
        renderer.DrawText(words_[i].text, draw,
                          mappable ? word_style : outside_style);
    }
    for (const auto& [gy, stamp] : gutter_marks_) {
        const float ry = top + gy - scroll_y_;
        if (ry < top - 20.0f || ry > bottom) continue;
        renderer.DrawText(stamp,
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
        const float ty = top +
            (track_h - thumb) *
                (MaxScroll() > 0.0f ? scroll_y_ / MaxScroll() : 0.0f);
        renderer.FillRect(
            {bounds.x + bounds.width - 4.0f, ty, 3.0f, thumb},
            tokens.border, 1.5f);
    }
}

void TranscriptPanelWidget::Layout(
    const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;

    // Controls row under the drawn header: the Transcribe button on
    // the right of the strip.
    const float ctrl_y = bounds.y + 34.0f;
    const float btn_w = 110.0f;
    transcribe_btn_->Layout(
        {bounds.x + bounds.width - pad - btn_w, ctrl_y, btn_w,
         VeTheme::kControlHeight},
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

    // Caption-style picker on the left of the same controls row.
    caption_style_combo_->Layout(
        {bounds.x + pad, ctrl_y, 136.0f, VeTheme::kControlHeight},
        tokens);
    caption_style_combo_->SetVisible(!media_path_.empty());

    // Bottom action bar — only while a word range is selected.
    const bool sel = HasSelection();
    const float bar_y = bounds.y + bounds.height - 30.0f -
                        (sel ? VeTheme::kControlHeight +
                                   VeTheme::kSpaceXs
                             : 0.0f);
    if (sel) {
        float ax = bounds.x + pad;
        cut_btn_->Layout({ax, bar_y, 96.0f, VeTheme::kControlHeight},
                         tokens);
        ax += 100.0f;
        marker_btn_->Layout({ax, bar_y, 76.0f, VeTheme::kControlHeight},
                            tokens);
        ax += 80.0f;
        captions_btn_->Layout({ax, bar_y, 76.0f,
                               VeTheme::kControlHeight},
                              tokens);
        // "Ripple delete" needs the clip mapping; captions needs a
        // bound clip for placement. Marker too (timeline seconds).
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

bool TranscriptPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    namespace Widgets = XER::UI::Widgets;
    switch (event.type) {
        case Widgets::Event::Type::MouseWheel: {
            const float range = MaxScroll();
            if (range <= 0.0f) return false;
            scroll_y_ = std::clamp(
                scroll_y_ - static_cast<float>(event.wheel_delta) * 0.5f,
                0.0f, range);
            follow_playhead_ = false;   // user scroll wins until a seek
            Invalidate();
            return true;
        }
        case Widgets::Event::Type::MouseMove: {
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
        case Widgets::Event::Type::MouseDown: {
            if (event.mouse_button != Widgets::MouseButton::Left) {
                return false;
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
        case Widgets::Event::Type::MouseUp: {
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
        case Widgets::Event::Type::Click: {
            if (suppress_click_) {
                suppress_click_ = false;
                return true;
            }
            const int w = WordAt(event.position);
            if (w < 0) return false;
            sel_a_ = sel_b_ = -1;      // click = caret, not a range
            follow_playhead_ = true;
            const auto& word = words_[static_cast<std::size_t>(w)];
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
            return true;
        }
        case Widgets::Event::Type::KeyDown: {
            if (event.key == Widgets::KeyCode::Escape &&
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

void TranscriptPanelWidget::SetStatus(const std::string& text,
                                      bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void TranscriptPanelWidget::Tick(double dt_seconds) {
    (void)dt_seconds;
}

}  // namespace XER::VideoEditor::Ui
