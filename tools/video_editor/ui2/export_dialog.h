// ui2 export-settings dialog — the form the shell hosts inside a kit
// Dialog ("Export" / "Cancel") before the job lands on QueuePanel.
// Ported from the legacy ui/ExportDialog's *configuration* half: the
// ui2 queue owns progress/cancel after submit (the panel drives the
// real ExportEngine), so this widget's job ends at producing a
// validated ExportRequest.
//
//   Preset     [YouTube 1080p          ▼]
//   Format     [MP4 (.mp4)             ▼]
//   Resolution [Match source] W [1920] H [1080]
//   Frame rate [Project 30 fps         ▼]
//   Video      [H.264 (libx264)        ▼]
//   Rate       [Bitrate] [ 8 ] Mbps
//   Audio      [AAC] [192] kbps
//   Range      [Entire sequence        ▼]
//   Output     [C:/…/out.mp4     ] [Browse…]
//   ~34.2 MB estimated · error line
//
// Field wiring is live: the format Select rebuilds the codec lists
// (webm -> libvpx-vp9/libopus), the preset Select stamps codec/
// bitrate/scale, and every control refreshes the estimated-size line.
// Validate() returns the first problem (empty output path, bad
// custom range, unsupported combo) — the shell calls it from the
// dialog's Accepted result before composing the QueueJob.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "export_engine.h"
#include "export_preset.h"
#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Checkbox;
class Label;
class ScrubNumberField;
class Select;
class TextInput;
}

namespace XER::VideoEditor::Ui2 {

// Everything the shell needs to compose a QueueJob + ExportOptions.
// Field semantics mirror ExportOptions: rate-control is EITHER
// video_bit_rate (video_crf < 0) or video_crf (>= 0 wins); fps/w/h
// of 0 mean "project native" (the engine derives canvas/fps from the
// project snapshot — the shell rewrites the snapshot for overrides).
struct ExportRequest {
    std::string container = "mp4";       // mp4 | mov | webm
    int width = 0;                       // 0 = project native
    int height = 0;
    int fps_num = 0;                     // 0 = project native
    int fps_den = 1;
    std::string video_codec = "libx264"; // "" = audio only
    int video_bit_rate = 8000000;        // bps
    int video_crf = -1;                  // >=0 selects the CRF path
    std::string audio_codec = "aac";
    int audio_bit_rate = 192000;         // bps
    int audio_sample_rate = 0;           // 0 = 48 kHz export default
    enum class Range {
        WholeTimeline,                   // ignores the work area
        WorkArea,                        // project.work_in/work_out
        Custom                           // explicit in/out seconds
    };
    Range range = Range::WholeTimeline;
    double range_in = 0.0;
    double range_out = 0.0;
    std::string output_path;             // UTF-8
    std::string preset_name;             // display bookkeeping
};

// The form content widget. The shell wraps it in W::Dialog::Create —
// ExportDialog is deliberately Dialog-agnostic so tests can drive it
// without an overlay tree.
class ExportDialog final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<ExportDialog> Create();

    // Pushes project-derived defaults (canvas, fps, work area,
    // duration) used by the resolution/fps "match" rows, the range
    // estimate and the output-path suggestion. Non-owning — re-push
    // whenever the document changes.
    void SetProject(const Project* project);
    // Prefills every control (e.g. the last request the user made).
    void SetRequest(const ExportRequest& request);
    // Reads the controls back — values are clamped/sanitized, never
    // throws. Does NOT imply Validate() passes.
    ExportRequest GetRequest() const;

    // First blocking problem with the current request, or "" — also
    // publishes it on the error line. Headless-safe.
    std::string Validate();
    const std::string& ErrorText() const { return error_text_; }

    // "H.264 MP4 · 8 Mbps" / "VP9 WebM · CRF 32" — the queue row's
    // format column.
    std::string FormatSummary() const;
    // "entire sequence" / "work area 00:00–00:42" / "custom 5.0–9.0" —
    // the queue row's detail column (timecoded at project fps).
    std::string RangeSummary() const;
    // Cheap estimate: (video+audio bitrate) * covered seconds / 8.
    // CRF exports report the bitrate field's value as the guess.
    double EstimatedBytes() const;
    // Covered range in timeline seconds [in,out) — resolved the same
    // way the engine resolves it (Whole -> [0, duration], WorkArea ->
    // the set work area else whole, Custom -> clamped pair).
    std::pair<double, double> ResolvedRangeSeconds() const;

    // The host's file picker — receives the container extension
    // ("mp4") and the suggested path, returns the picked path (""
    // cancelled). Unset -> Browse() is a no-op.
    void SetBrowseHandler(
        std::function<std::string(const std::string& extension,
                                  const std::string& suggested_path)>
            handler);
    // Headless seam: same path the Browse button takes.
    void Browse();

    // ---- headless field drive (same setters the widgets fire) -----
    void SelectFormatIndex(int index);
    void SelectPresetIndex(int index);
    void SelectResolutionIndex(int index);   // -1 -> Custom fields
    void SelectFpsIndex(int index);          // -1 -> Custom num/den
    void SelectVideoCodecIndex(int index);
    void SelectAudioCodecIndex(int index);
    void SelectRateModeIndex(int index);     // 0 bitrate, 1 CRF
    void SelectRangeIndex(int index);
    void SetWidthHeight(int width, int height);
    void SetCustomFps(int fps_num, int fps_den);
    void SetRateValue(float value);          // Mbps or CRF per mode
    void SetAudioKbps(float kbps);
    void SetCustomRange(double in_seconds, double out_seconds);
    void SetOutputPath(const std::string& utf8_path);
    // Preset list the combo shows (builtins then user presets).
    int PresetCount() const { return static_cast<int>(presets_.size()); }
    const ExportPreset& PresetAt(int index) const;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {360.0f, 430.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {430.0f, 452.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    ExportDialog() = default;

    struct FormatSpec {
        std::string label;                    // "MP4 (.mp4)"
        std::string extension;                // "mp4"
        std::vector<std::string> video_codecs;  // display labels
        std::vector<std::string> video_names;   // encoder names
        std::vector<std::string> audio_codecs;
        std::vector<std::string> audio_names;
    };
    static const std::vector<FormatSpec>& FormatSpecs();

    void EnsureChildren();
    void SyncCodecOptions();      // format -> codec option lists
    void SyncRateMode();          // bitrate/CRF label + unit swap
    void SyncResolutionEnable();  // preset vs custom w/h fields
    void SyncRangeEnable();       // custom in/out enablement
    void RefreshEstimate();
    void SetError(std::string text);
    void ApplyPreset(const ExportPreset& preset);
    void RebuildPresetList();
    // Stamps `extension` onto output_path when the basename's suffix
    // disagrees (keeps the user's directory + stem).
    std::string PathWithExtension(const std::string& path,
                                  const std::string& extension) const;
    int FormatIndex() const;     // index into FormatSpecs()

    const Project* project_ = nullptr;
    bool children_attached_ = false;
    std::vector<ExportPreset> presets_;
    std::string error_text_;
    std::function<std::string(const std::string&,
                              const std::string&)>
        browse_;

    std::shared_ptr<XER::UI::Widgets::Label> estimate_;
    std::shared_ptr<XER::UI::Widgets::Label> error_;
    std::shared_ptr<XER::UI::Widgets::Select> preset_select_;
    std::shared_ptr<XER::UI::Widgets::Select> format_select_;
    std::shared_ptr<XER::UI::Widgets::Select> resolution_select_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> width_field_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> height_field_;
    std::shared_ptr<XER::UI::Widgets::Select> fps_select_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> fps_num_field_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> fps_den_field_;
    std::shared_ptr<XER::UI::Widgets::Select> vcodec_select_;
    std::shared_ptr<XER::UI::Widgets::Select> rate_mode_select_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> rate_field_;
    std::shared_ptr<XER::UI::Widgets::Label> rate_unit_;
    std::shared_ptr<XER::UI::Widgets::Select> acodec_select_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> akbps_field_;
    std::shared_ptr<XER::UI::Widgets::Select> range_select_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> range_in_field_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> range_out_field_;
    std::shared_ptr<XER::UI::Widgets::TextInput> output_input_;
    std::shared_ptr<XER::UI::Widgets::Button> browse_button_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> audio_only_check_;
};

}  // namespace XER::VideoEditor::Ui2
