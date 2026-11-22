// TranscriptPanel — the M3 wave-2 transcript dock (spec §6 "Trn"
// tab), the flagship panel: a Descript / Premiere "Paper Edit"
// surface rebuilt on the shared kit where the words ARE the edit
// points.
//
//   Transcript — interview.mp4      [style ▾] [Transcribe]
//   Search…                       [<] 3/14 [>]   [◂ Seg] [Seg ▸]
//   00:00  so today we're going to talk about the new
//          pipeline and how the renders come together
//   00:07  um basically it's three stages — decode,
//          composite, encode
//   [Ripple delete] [+ Marker] [Captions]   (selection action bar)
//
// Interactions (legacy TranscriptPanelWidget parity, plus the M3
// additions marked *):
//   * click a word           -> seek the playhead to its timeline
//                               time through the bound clip's
//                               source->timeline map
//   * drag across words      -> a word range; the action bar offers
//                               Ripple delete (extract), + Marker,
//                               Captions
//   * playback               -> the word under the playhead
//                               highlights (SetPlayhead feed)
//   * search field (*)       -> case-insensitive word matches, amber
//                               wash, prev/next cycling that scrolls
//                               the match into view
//   * segment nav (*)        -> "◂ Seg"/"Seg ▸" buttons and the
//                               gutter timestamps jump between
//                               whisper segment starts
//   * speaker labels         -> NOT drawn: TranscriptSegment has no
//                               speaker field (diarization lands with
//                               the whisper-gpu wave — an honest
//                               omission, not a fake column)
//
// Data flow (surface_hooks.h rules): the shell pushes the subject
// (SetSubject) and the transcript (SetSegments/SetWords), and every
// action leaves through Hooks — the panel never touches
// TimelineHistory, ExtHost or the whisper worker itself.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ai/whisper_transcribe.h"   // Ai::TranscriptSegment
#include "layout/geometry.h"
#include "timeline_model.h"          // Clip (bound-subject snapshot)
#include "transcript_edit.h"         // TranscriptWord
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Combobox;
class Label;
class TextInput;
}

namespace XER::VideoEditor::Ui2 {

class TranscriptPanel final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<TranscriptPanel> Create();

    struct Hooks {
        // Click-seek / playback-follow target — TIMELINE seconds.
        std::function<void(double)> on_seek;
        // Kick the async whisper job for the bound subject's media.
        std::function<void()> on_transcribe;
        // Selection -> extract: TIMELINE range, close the gap
        // (history_->LiftRange close_gap=true on the shell side).
        std::function<void(double, double)> on_ripple_delete;
        // Marker at the selection head (timeline s, selected text).
        std::function<void(double, const std::string&)> on_add_marker;
        // Caption clips for the selection — SOURCE-time range (the
        // shell re-maps through the bound clip + BuildCaptionClips).
        std::function<void(double, double)> on_make_captions;
        // Caption-style picker changed: "classic" | "karaoke" |
        // "wordpop" (CaptionPresetByName on the shell side).
        std::function<void(const std::string&)> on_caption_style;
        // whisper DLL + model check — drives the button's enabled
        // state so an uninstalled backend reads as a hint, not an
        // error.
        std::function<bool()> transcribe_ready;
        std::function<void(const std::string&, bool)> on_status;
    };
    void SetHooks(Hooks hooks);
    const Hooks& GetHooks() const { return hooks_; }

    // ---- subject + transcript feed ------------------------------
    // `clip` (nullable, copied) supplies the source->timeline
    // mapping; empty media_path clears the panel (bin-bound mode
    // renders words but can't seek/edit).
    void SetSubject(std::string media_path, std::string display_name,
                    const Clip* clip);
    const std::string& MediaPath() const { return media_path_; }
    // Primary feed: whisper segments (word spans pass through;
    // segment-less timing falls back to proportional distribution
    // inside TranscriptWordsFromSegments).
    void SetSegments(std::vector<Ai::TranscriptSegment> segments);
    // Direct word feed — the smoke/tests path and the cache replay.
    void SetWords(std::vector<TranscriptWord> words);
    int SegmentCount() const {
        return static_cast<int>(segments_.size());
    }
    int WordCount() const { return static_cast<int>(words_.size()); }
    const std::vector<TranscriptWord>& Words() const {
        return words_;
    }

    // ---- async job + playback ------------------------------------
    // Job state pushed by the shell each tick (progress bar +
    // button disable while a job is in flight).
    void SetJobState(bool running, float progress);
    void SetPlayhead(double timeline_seconds);
    double Playhead() const { return playhead_; }
    // Word whose source span currently contains the playhead (-1).
    int CurrentWord() const;

    // ---- word-range selection -------------------------------------
    bool HasSelection() const;
    void ClearSelection();
    std::string SelectedText() const;
    bool SelectionSourceRange(double* t0, double* t1) const;
    bool SelectionTimelineRange(double* t0, double* t1) const;
    // Programmatic word selection — the verb the drag gesture and
    // the smoke share.
    void SelectWordRange(int a, int b);
    // The seek path click/double-click share: word index -> mapped
    // timeline seconds through on_seek. False when the word is
    // outside the bound clip's consumed span.
    bool ActivateWord(int index);
    // Action-bar verbs (button OnClick + smoke funnel through these).
    bool RippleDeleteSelection();
    bool AddMarkerForSelection();
    bool MakeCaptionsForSelection();

    // ---- search (M3 addition) --------------------------------------
    void SetSearchText(std::string text);
    const std::string& SearchText() const { return search_; }
    int MatchCount() const {
        return static_cast<int>(matches_.size());
    }
    int CurrentMatch() const { return match_index_; }  // -1 = none
    // Step to the next/prev match (wraps) — the toolbar [<][>].
    bool StepMatch(int direction);

    // ---- segment navigation (M3 addition) --------------------------
    // Index of the segment whose span contains `src_t` (-1 outside).
    int SegmentAtSourceTime(double src_t) const;
    // Seek to a segment start through the bound clip (same map the
    // word seek uses). False on unmapped/out-of-range.
    bool JumpToSegment(int index);
    bool StepSegment(int direction);   // relative to the playhead

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {240.0f, 180.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {340.0f, 340.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit TranscriptPanel();

    void EnsureChildren();
    // Word flow (MeasureText needs the renderer) — recomputed
    // lazily in Render when the width or word set changed; hit
    // testing reads the cached result.
    void EnsureFlow(XER::UI::Rendering::Renderer& renderer,
                    const XER::UI::Theme::Tokens& tokens);
    int WordAt(const XER::UI::Layout::Point& position) const;
    int WordAtSourceTime(double src_t) const;
    // Playhead as SOURCE seconds through the bound clip (media-bound
    // mode treats the playhead as media time).
    double PlayheadSource() const;
    float TextAreaTop() const;
    float TextViewportBottom() const;
    float MaxScroll() const;
    void ScrollToContentY(float y, float margin);
    void RebuildMatches();
    void SyncToolbar();
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    std::string media_path_;
    std::string display_name_;
    bool has_clip_ = false;
    Clip clip_;                       // mapping snapshot
    std::vector<Ai::TranscriptSegment> segments_;
    std::vector<TranscriptWord> words_;
    double playhead_ = 0.0;
    bool job_running_ = false;
    float job_progress_ = 0.0f;

    // Selection (word indices; sel_a_ anchor, sel_b_ cursor).
    int sel_a_ = -1;
    int sel_b_ = -1;
    bool dragging_ = false;
    bool suppress_click_ = false;    // multi-word drag ended
    int hovered_word_ = -1;

    // Search state — indices into words_ (sorted).
    std::string search_;
    std::vector<int> matches_;
    int match_index_ = -1;

    float scroll_y_ = 0.0f;
    bool follow_playhead_ = true;    // auto-scroll to the live word

    // Flow cache — content-space word rects (x absolute to the
    // panel, y relative to TextAreaTop before scroll).
    bool flow_valid_ = false;
    float flow_width_ = -1.0f;
    std::vector<XER::UI::Layout::Rect> word_rects_;
    // (content y, "MM:SS", word index) — one per segment start,
    // drawn + clickable in the left timestamp gutter.
    struct GutterMark {
        float y = 0.0f;
        std::string stamp;
        int word_index = -1;
        int segment_index = -1;
    };
    std::vector<GutterMark> gutter_marks_;
    float flow_height_ = 0.0f;
    float text_x_ = 0.0f;
    float gutter_w_ = 46.0f;

    std::shared_ptr<XER::UI::Widgets::TextInput> search_input_;
    std::shared_ptr<XER::UI::Widgets::Button> prev_match_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> next_match_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> match_count_;
    std::shared_ptr<XER::UI::Widgets::Button> prev_seg_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> next_seg_btn_;
    std::shared_ptr<XER::UI::Widgets::Combobox> caption_style_combo_;
    std::shared_ptr<XER::UI::Widgets::Button> transcribe_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> cut_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> marker_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> captions_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    bool children_attached_ = false;
};

}  // namespace XER::VideoEditor::Ui2
