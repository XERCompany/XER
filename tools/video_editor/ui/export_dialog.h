// Modal export overlay shown by the editor shell while ExportEngine
// renders in the background. No engine ownership here — SetProgress()
// is fed by the app on the UI thread and Cancel/Close just invokes a hook.
//
// Phase-2 additions: a Premiere-style range section (Entire sequence /
// Work area / Custom in-out), an estimated duration + resolution line,
// and SetProject()/GetRange() so the shell can feed the chosen range
// into ExportOptions::range_in/range_out before starting the engine.
// Phase-2c: a Preset section (builtin + user presets) feeding
// codec/bitrate/scale into GetExportOptions().
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "export_engine.h"
#include "export_preset.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Combobox;
class Label;
class Progress;
class RadioGroup;
class TextInput;
}

namespace XER::VideoEditor::Ui {

// Which portion of the timeline the export covers.
enum class ExportRangeMode {
    EntireSequence,  // whole project — GetRange() returns {0, 0}
    WorkArea,        // project.work_in/work_out (disabled when unset)
    Custom           // explicit in/out seconds from the numeric fields
};

class ExportDialog final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        std::function<void()> on_cancel_or_close;
        // Fired when the dialog is in its pre-export configuration
        // state (Reset() called, no progress yet) and the action
        // button is clicked — the app should start the export with
        // GetRange()/GetRangeMode(). Unset -> falls back to
        // on_cancel_or_close.
        std::function<void()> on_export_start;
    };

    explicit ExportDialog(Hooks hooks = {});

    // Project the range section describes (work-area availability,
    // duration/resolution estimate). Non-owning — call again whenever
    // the project or its work area changes.
    void SetProject(const Project* project);
    // Selects Custom mode and fills the in/out fields (seconds).
    void SetRange(double in_seconds, double out_seconds);
    void SetRangeMode(ExportRangeMode mode);
    ExportRangeMode GetRangeMode() const { return range_mode_; }
    // Resolved [in, out) in timeline seconds, same convention as
    // ExportOptions::range_in/range_out: out <= in means "whole
    // project". Entire-sequence mode returns {0, 0}.
    std::pair<double, double> GetRange() const;

    // The preset currently selected in the Preset section (builtin or
    // user-loaded). Always valid — defaults to the first builtin.
    const ExportPreset& SelectedPreset() const { return selected_preset_; }
    // Merges the selected preset with the resolved range — everything
    // BeginConfiguredExport() needs except output_path (the app fills
    // that after the file pick). Range semantics mirror the mode
    // switch the app used to do itself: EntireSequence resolves to an
    // explicit {0, project duration} so it still overrides the work
    // area; WorkArea leaves {0, 0} for the engine to resolve.
    ExportOptions GetExportOptions() const;

    // Overrides the user-preset JSON file (default:
    // DefaultPresetsPath() — per-user, project-independent). Pass an
    // empty string to restore the default. Reloads immediately if the
    // dialog is already built.
    void SetPresetsPath(std::string utf8_path);
    // Re-reads the preset file and rebuilds the dropdown (builtins
    // first, then user presets — a user preset may shadow a builtin
    // name; the combo keeps them distinct by index).
    void ReloadPresets();

    void Reset();
    void SetProgress(const ExportProgress& progress);
    void SetFinished(ExportStatus status, const std::string& message);
    bool Finished() const { return finished_; }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void EnsureChildren();
    // Recomputes the work-area radio option, field enablement and the
    // duration/resolution estimate line.
    void RefreshRangeUi();
    bool WorkAreaSet() const;
    double ParsedInSeconds() const;
    double ParsedOutSeconds() const;
    void SetRangeControlsEnabled(bool enabled);
    // Rebuilds presets_ (builtins + file), the combo options and the
    // codec/bitrate readout for the current selection.
    void RebuildPresetList();
    void RefreshPresetUi();
    const std::string& PresetsPathOrDefault() const;

    Hooks hooks_;
    bool children_attached_ = false;
    bool finished_ = false;
    bool running_ = false;   // progress reported, not yet finished
    ExportProgress progress_;
    std::string message_ = "Preparing export…";

    const Project* project_ = nullptr;   // non-owning
    ExportRangeMode range_mode_ = ExportRangeMode::EntireSequence;
    // -1 = radio options not built yet; 0/1 = built for work-area
    // unset/set (avoids a relayout request per refresh).
    int built_options_state_ = -1;

    // Builtins + user presets (ReloadPresets). selected_preset_ is a
    // copy — a stable value even after the file reloads, so
    // SelectedPreset()/GetExportOptions() never dangle.
    std::vector<ExportPreset> presets_;
    ExportPreset selected_preset_ = [] {
        const std::vector<ExportPreset> builtins = BuiltinPresets();
        return builtins.empty() ? ExportPreset{} : builtins.front();
    }();
    std::string presets_path_;          // empty -> DefaultPresetsPath()
    // Cached once (env read) so per-frame RefreshPresetUi stays cheap.
    std::string presets_path_default_ = DefaultPresetsPath();

    std::shared_ptr<XER::UI::Widgets::Label> preset_label_;
    std::shared_ptr<XER::UI::Widgets::Combobox> preset_combo_;
    std::shared_ptr<XER::UI::Widgets::Label> preset_info_;
    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Label> range_label_;
    std::shared_ptr<XER::UI::Widgets::RadioGroup> range_radio_;
    std::shared_ptr<XER::UI::Widgets::Label> in_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> in_input_;
    std::shared_ptr<XER::UI::Widgets::Label> out_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> out_input_;
    std::shared_ptr<XER::UI::Widgets::Label> info_label_;
    std::shared_ptr<XER::UI::Widgets::Label> status_label_;
    std::shared_ptr<XER::UI::Widgets::Progress> bar_;
    std::shared_ptr<XER::UI::Widgets::Button> action_;
};

}  // namespace XER::VideoEditor::Ui
