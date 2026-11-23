// ui2 project-settings dialog — port of the legacy ui/SettingsDialog
// onto the shared kit. The shell hosts this form inside a kit Dialog
// ("Apply"/"Cancel") and commits the result two ways, matching the
// legacy split:
//
//   * document fields (canvas w/h, fps, proxy workflow) go through a
//     TimelineHistory command — undoable + serialized into .xerproj;
//   * app fields (display name, audio sample rate, scratch dir,
//     transcript cap, autosave) land on ShellPrefs — they are
//     project-independent and never undoable.
//
//   ── Sequence ─────────────────────────────
//   Name        [interview-cut           ]
//   Resolution  [1920 x 1080 ▼] W [1920] H [1080]
//   Frame rate  [30 ▼] [30]/[1]
//   Audio rate  [48000 Hz                ▼]
//   ── Media & cache ────────────────────────
//   [x] Decode via proxies   [ ] Build proxies on import
//   Proxy       dim [960]px  crf [28]  cap [20480] MiB
//   Scratch dir [                        ]
//   Transcript  cap [60] min   Autosave [x] every [120] s
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "sequence_preset.h"
#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Checkbox;
class Label;
class ScrubNumberField;
class Select;
class TextInput;
}

namespace XER::VideoEditor::Ui2 {

// The dialog's read-back payload. `project_*` fields map 1:1 onto
// Project members (timeline_model.h); the rest are app-level prefs.
struct Ui2ProjectSettings {
    // ---- document (undoable, serialized) ----
    int width = 1920;
    int height = 1080;
    int fps_num = 30;
    int fps_den = 1;
    bool use_proxies = true;
    bool proxy_ingest = false;
    int proxy_max_dim = 960;         // long-edge cap, px
    int proxy_crf = 28;              // h264 constant-quality target
    int proxy_cache_max_mb = 20480;  // LRU cap (0 = unbounded)

    // ---- app-level (ShellPrefs, immediate) ----
    std::string display_name;        // save-as basename suggestion
    int audio_sample_rate = 48000;   // export mix rate
    std::string scratch_dir;         // "" = system default
    double transcript_max_minutes = 0.0;  // 0 = engine default
    bool autosave_enabled = true;
    int autosave_interval_s = 120;
};

class ProjectSettingsDialog final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<ProjectSettingsDialog> Create();

    // Fills every field — call before showing. `project` supplies the
    // document fields (nullptr leaves the last values).
    void SetProject(const Project* project);
    void SetSettings(const Ui2ProjectSettings& settings);
    Ui2ProjectSettings GetSettings() const;

    // First blocking problem ("" when clean) — also publishes it on
    // the error line.
    std::string Validate();
    const std::string& ErrorText() const { return error_text_; }

    // ---- headless field drive --------------------------------------
    void SetName(const std::string& name);
    void SelectResolutionIndex(int index);   // preset rows + Custom
    void SetWidthHeight(int width, int height);
    void SelectFpsIndex(int index);
    void SetCustomFps(int fps_num, int fps_den);
    void SelectSampleRateIndex(int index);
    void SetUseProxies(bool on);
    void SetProxyIngest(bool on);
    void SetProxyMaxDim(int px);
    void SetProxyCrf(int crf);
    void SetProxyCacheCap(int mib);
    void SetScratchDir(const std::string& utf8_dir);
    void SetTranscriptCapMinutes(double minutes);
    void SetAutosaveEnabled(bool on);
    void SetAutosaveInterval(int seconds);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {380.0f, 400.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {440.0f, 420.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    ProjectSettingsDialog() = default;

    void EnsureChildren();
    void SyncCustomEnable();
    void SetError(std::string text);
    // The resolution preset row list: BuiltinSequencePresets() names
    // then "Custom" — index helpers keep the select in sync.
    int ResolutionRowFor(int w, int h, int fps_num, int fps_den) const;
    std::vector<SequencePreset> res_presets_;

    bool children_attached_ = false;
    std::string error_text_;

    std::shared_ptr<XER::UI::Widgets::TextInput> name_input_;
    std::shared_ptr<XER::UI::Widgets::Select> res_select_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> width_field_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> height_field_;
    std::shared_ptr<XER::UI::Widgets::Select> fps_select_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> fps_num_field_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> fps_den_field_;
    std::shared_ptr<XER::UI::Widgets::Select> rate_select_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> proxies_check_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> ingest_check_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> proxy_dim_field_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> proxy_crf_field_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> proxy_cap_field_;
    std::shared_ptr<XER::UI::Widgets::TextInput> scratch_input_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField>
        transcript_cap_field_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> autosave_check_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField>
        autosave_interval_field_;
    std::shared_ptr<XER::UI::Widgets::Label> error_;
};

}  // namespace XER::VideoEditor::Ui2
