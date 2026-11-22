// Transcript panel — text-based editing surface (Descript / Premiere
// "Paper Edit"): a scrollable whisper transcript where the words ARE
// the edit points.
//
//   Transcript — interview.mp4            [Transcribe]
//   00:00  so today we're going to talk about the new
//          pipeline and how the renders come together
//   00:07  um basically it's three stages — decode,
//          composite, encode
//
// The words flow as wrapped text; each word carries its source-time
// span (whisper gives segment spans; word timing is distributed —
// see transcript_edit.h). Interactions:
//   * click a word           -> seek the playhead to its timeline time
//   * drag across words      -> selects a range; an action bar offers
//                               Ripple delete (extract), + Marker,
//                               Captions
//   * playback               -> the word under the playhead highlights
//
// The panel is bound to a subject: a timeline Clip (preferred — its
// in_point/rate maps source<->timeline) or a bare media path (the
// transcript still renders/seeks nothing; bin-bound mode). All edits
// and the transcription job itself go through Hooks — the widget
// never touches the model directly.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "timeline_model.h"
#include "transcript_edit.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Combobox;
class Label;
}

namespace XER::VideoEditor::Ui {

class TranscriptPanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Click-seek / playback-follow target — TIMELINE seconds.
        std::function<void(double)> on_seek;
        // Kick the async whisper job for the bound subject's media.
        std::function<void()> on_transcribe;
        // Selection -> extract: timeline range, close the gap
        // (history_.LiftRange close_gap=true on the app side).
        std::function<void(double, double)> on_ripple_delete;
        // Marker at the selection head (timeline s, selected text).
        std::function<void(double, const std::string&)> on_add_marker;
        // Caption clips for the selection — SOURCE-time range (the
        // app re-maps through the bound clip).
        std::function<void(double, double)> on_make_captions;
        // Caption-style picker changed: "classic" | "karaoke" |
        // "wordpop" (CaptionPresetByName on the app side).
        std::function<void(const std::string&)> on_caption_style;
        // whisper DLL + model check — drives the button's enabled
        // state so an uninstalled backend reads as a hint, not an
        // error.
        std::function<bool()> transcribe_ready;
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit TranscriptPanelWidget(Hooks hooks = {});

    // Rebind the panel: `media_path`/`display_name` identify the
    // subject; `clip` (nullable, copied) supplies the source->timeline
    // mapping. Empty media_path clears the panel.
    void SetSubject(std::string media_path, std::string display_name,
                    const Clip* clip);
    // The transcript for the current subject — empty vector means
    // "not transcribed" (the Transcribe button shows).
    void SetWords(std::vector<TranscriptWord> words);
    // Async job state pushed by the app each tick (progress bar +
    // button disable while a job is in flight).
    void SetJobState(bool running, float progress);
    void SetPlayhead(double timeline_seconds);
    double Playhead() const { return playhead_; }

    // Word-range selection state.
    bool HasSelection() const;
    void ClearSelection();
    std::string SelectedText() const;
    // Selected source-time range (whole words), false when no
    // selection.
    bool SelectionSourceRange(double* t0, double* t1) const;
    // Selection mapped to timeline time through the bound clip —
    // false when no selection, no clip binding, or the range falls
    // fully outside the clip's consumed source span.
    bool SelectionTimelineRange(double* t0, double* t1) const;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {220.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {300.0f, 260.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void EnsureChildren();
    // Word flow (MeasureText needs the renderer) — recomputed lazily
    // in Render when the width or word set changed; hit-testing reads
    // the cached result.
    void EnsureFlow(XER::UI::Rendering::Renderer& renderer,
                    const XER::UI::Theme::Tokens& tokens);
    int WordAt(const XER::UI::Layout::Point& position) const;
    // Word whose source span contains `src_t` (-1 outside words_).
    int WordAtSourceTime(double src_t) const;
    // Playhead as SOURCE seconds through the bound clip (NaN when the
    // playhead sits outside the clip — also for media-bound mode,
    // where the playhead is treated as media time).
    double PlayheadSource() const;
    float TextAreaTop() const;
    float TextViewportBottom() const;
    float MaxScroll() const;
    void SetStatus(const std::string& text, bool is_error = false);
    int CurrentWord() const;

    Hooks hooks_;
    std::string media_path_;
    std::string display_name_;
    bool has_clip_ = false;
    Clip clip_;                      // mapping snapshot (has_clip_)
    std::vector<TranscriptWord> words_;
    double playhead_ = 0.0;
    bool job_running_ = false;
    float job_progress_ = 0.0f;

    int hovered_word_ = -1;
    int sel_a_ = -1;                 // anchor word index (-1 = none)
    int sel_b_ = -1;                 // cursor word index
    bool dragging_ = false;
    bool suppress_click_ = false;    // multi-word drag ended
    float scroll_y_ = 0.0f;
    bool follow_playhead_ = true;    // auto-scroll to the live word

    // Flow cache — content-space word rects (x absolute to the panel,
    // y relative to TextAreaTop before scroll).
    bool flow_valid_ = false;
    float flow_width_ = -1.0f;
    std::vector<XER::UI::Layout::Rect> word_rects_;
    // (content y, "MM:SS") — one per segment start, drawn in the
    // timestamp gutter left of the text column.
    std::vector<std::pair<float, std::string>> gutter_marks_;
    float flow_height_ = 0.0f;
    float text_x_ = 0.0f;            // text column left edge
    float gutter_w_ = 44.0f;

    std::shared_ptr<XER::UI::Widgets::Button> transcribe_btn_;
    std::shared_ptr<XER::UI::Widgets::Combobox> caption_style_combo_;
    std::shared_ptr<XER::UI::Widgets::Button> cut_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> marker_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> captions_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    bool children_attached_ = false;
};

}  // namespace XER::VideoEditor::Ui
