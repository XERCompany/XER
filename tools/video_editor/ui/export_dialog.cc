#include "export_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/combobox.h"
#include "widgets/label.h"
#include "widgets/progress.h"
#include "widgets/radio_group.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

std::string Percent(double fraction) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d%%",
                  static_cast<int>(
                      std::clamp(fraction, 0.0, 1.0) * 100.0 + 0.5));
    return buffer;
}

// Lenient numeric field parse: empty/garbage -> 0, no exceptions.
double ParseSeconds(const std::string& text) {
    if (text.empty()) return 0.0;
    const double value = std::strtod(text.c_str(), nullptr);
    if (!std::isfinite(value) || value < 0.0) return 0.0;
    return value;
}

std::string FormatSeconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", seconds);
    return buffer;
}

std::string FormatDuration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rem = seconds - minutes * 60.0;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%05.2f", minutes, rem);
    return buffer;
}

std::string FormatFps(int fps_num, int fps_den) {
    if (fps_num <= 0 || fps_den <= 0) return "?";
    if (fps_den == 1) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d", fps_num);
        return buffer;
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%g",
                  static_cast<double>(fps_num) / fps_den);
    return buffer;
}

// Radio option indices (match range_radio_ options order).
constexpr int kRangeEntire = 0;
constexpr int kRangeWorkArea = 1;
constexpr int kRangeCustom = 2;

// Single-line readout of the selected preset's encoder settings shown
// under the preset combobox — codec names, Mbps/kbit rates, and the
// canvas scale so the "Draft 720p"-style scaling is visible.
std::string DescribePreset(const ExportPreset& preset) {
    char buffer[192];
    if (preset.IsAudioOnly()) {
        std::snprintf(buffer, sizeof(buffer), "audio only · %s %dk",
                      preset.audio_codec.c_str(),
                      preset.audio_bit_rate / 1000);
        return buffer;
    }
    std::snprintf(
        buffer, sizeof(buffer), "%s · %.1f Mbps · %s %dk · %d%% canvas",
        preset.video_codec.c_str(), preset.video_bit_rate / 1.0e6,
        preset.audio_codec.c_str(), preset.audio_bit_rate / 1000,
        static_cast<int>(preset.resolution_scale * 100.0 + 0.5));
    return buffer;
}

constexpr float kPanelWidth = 480.0f;
constexpr float kPanelHeight = 416.0f;

}  // namespace

ExportDialog::ExportDialog(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(false);
}

void ExportDialog::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    title_ = std::make_shared<Widgets::Label>("Export timeline");
    range_label_ = std::make_shared<Widgets::Label>("Range", true);
    range_radio_ = std::make_shared<Widgets::RadioGroup>();
    range_radio_->SetOnChanged([this](int index) {
        if (index == kRangeWorkArea && !WorkAreaSet()) {
            // The work-area option is informational-only when the
            // project has no work area — snap back to Entire.
            range_radio_->SetSelectedIndex(kRangeEntire);
            return;
        }
        switch (index) {
            case kRangeWorkArea:
                range_mode_ = ExportRangeMode::WorkArea;
                break;
            case kRangeCustom:
                range_mode_ = ExportRangeMode::Custom;
                break;
            default:
                range_mode_ = ExportRangeMode::EntireSequence;
                break;
        }
        RefreshRangeUi();
    });
    preset_label_ = std::make_shared<Widgets::Label>("Preset", true);
    preset_combo_ = Widgets::Combobox::Create();
    preset_combo_->SetPlaceholder("Select preset…");
    preset_combo_->SetOnSelect(
        [this](const Widgets::Combobox::Option& option) {
            // option.value carries the presets_ index (see
            // RebuildPresetList) — the editable text is only a filter.
            const int index =
                std::max(0, std::atoi(option.value.c_str()));
            if (index >= 0 &&
                index < static_cast<int>(presets_.size())) {
                selected_preset_ = presets_[index];
            }
            RefreshPresetUi();
        });
    preset_info_ = std::make_shared<Widgets::Label>("", true);
    in_label_ = std::make_shared<Widgets::Label>("In", true);
    in_input_ = std::make_shared<Widgets::TextInput>("0.000");
    in_input_->SetOnValueChanged([this](const std::string&) {
        RefreshRangeUi();
    });
    out_label_ = std::make_shared<Widgets::Label>("Out", true);
    out_input_ = std::make_shared<Widgets::TextInput>("0.000");
    out_input_->SetOnValueChanged([this](const std::string&) {
        RefreshRangeUi();
    });
    info_label_ = std::make_shared<Widgets::Label>("", true);
    status_label_ = std::make_shared<Widgets::Label>("Preparing export…", true);
    bar_ = std::make_shared<Widgets::Progress>(0.0f);
    action_ = std::make_shared<Widgets::Button>(
        "Export", Widgets::ButtonStyle::Primary);
    action_->SetOnClick([this] {
        if (!running_ && !finished_ && hooks_.on_export_start) {
            hooks_.on_export_start();
        } else if (hooks_.on_cancel_or_close) {
            hooks_.on_cancel_or_close();
        }
    });
    AddChild(title_);
    AddChild(preset_label_);
    AddChild(preset_info_);
    AddChild(range_label_);
    AddChild(range_radio_);
    AddChild(in_label_);
    AddChild(in_input_);
    AddChild(out_label_);
    AddChild(out_input_);
    AddChild(info_label_);
    AddChild(status_label_);
    AddChild(bar_);
    AddChild(action_);
    // The combobox goes last: its dropdown overlays the range section,
    // and children are hit-tested/painted in add order (later = on
    // top).
    AddChild(preset_combo_);
    RebuildPresetList();
    RefreshRangeUi();
}

bool ExportDialog::WorkAreaSet() const {
    return project_ != nullptr && project_->work_out > project_->work_in;
}

void ExportDialog::SetProject(const Project* project) {
    project_ = project;
    if (children_attached_) {
        // Rebuild the option list so the work-area entry reflects the
        // new availability, then re-validate the current mode.
        RefreshRangeUi();
        if (range_mode_ == ExportRangeMode::WorkArea && !WorkAreaSet()) {
            SetRangeMode(ExportRangeMode::EntireSequence);
        }
    }
    Invalidate();
}

void ExportDialog::SetRange(double in_seconds, double out_seconds) {
    EnsureChildren();
    in_input_->SetValue(FormatSeconds(std::max(0.0, in_seconds)));
    out_input_->SetValue(FormatSeconds(std::max(0.0, out_seconds)));
    SetRangeMode(ExportRangeMode::Custom);
}

void ExportDialog::SetRangeMode(ExportRangeMode mode) {
    EnsureChildren();
    if (mode == ExportRangeMode::WorkArea && !WorkAreaSet()) {
        mode = ExportRangeMode::EntireSequence;
    }
    range_mode_ = mode;
    range_radio_->SetSelectedIndex(
        mode == ExportRangeMode::WorkArea    ? kRangeWorkArea
        : mode == ExportRangeMode::Custom    ? kRangeCustom
                                             : kRangeEntire);
    RefreshRangeUi();
}

std::pair<double, double> ExportDialog::GetRange() const {
    switch (range_mode_) {
        case ExportRangeMode::WorkArea:
            if (WorkAreaSet()) {
                return {project_->work_in, project_->work_out};
            }
            return {0.0, 0.0};
        case ExportRangeMode::Custom:
            return {ParsedInSeconds(), ParsedOutSeconds()};
        case ExportRangeMode::EntireSequence:
        default:
            return {0.0, 0.0};
    }
}

double ExportDialog::ParsedInSeconds() const {
    return in_input_ ? ParseSeconds(in_input_->GetValue()) : 0.0;
}

double ExportDialog::ParsedOutSeconds() const {
    return out_input_ ? ParseSeconds(out_input_->GetValue()) : 0.0;
}

void ExportDialog::SetRangeControlsEnabled(bool enabled) {
    if (range_radio_) range_radio_->SetEnabled(enabled);
    // The preset combobox locks on the same lifecycle as the range
    // controls — an export in flight must keep the options it started
    // with.
    if (preset_combo_) preset_combo_->SetEnabled(enabled);
    const bool custom = enabled &&
                        range_mode_ == ExportRangeMode::Custom;
    if (in_input_) in_input_->SetEnabled(custom);
    if (out_input_) out_input_->SetEnabled(custom);
}

const std::string& ExportDialog::PresetsPathOrDefault() const {
    return presets_path_.empty() ? presets_path_default_ : presets_path_;
}

void ExportDialog::SetPresetsPath(std::string utf8_path) {
    presets_path_ = std::move(utf8_path);
    if (children_attached_) {
        ReloadPresets();
    }
}

void ExportDialog::ReloadPresets() {
    RebuildPresetList();
    Invalidate();
}

void ExportDialog::RebuildPresetList() {
    // Builtins first (stable factory order), then whatever
    // LoadPresets() recovered — a missing/corrupt file just contributes
    // nothing rather than failing the dialog.
    presets_ = BuiltinPresets();
    std::vector<ExportPreset> user_presets;
    if (LoadPresets(PresetsPathOrDefault(), &user_presets, nullptr)) {
        for (ExportPreset& preset : user_presets) {
            presets_.push_back(std::move(preset));
        }
    }
    int selected_index = 0;
    if (presets_.empty()) {
        // Unreachable — BuiltinPresets() is never empty — but keeps
        // the combo/selection logic total if that ever changes.
        selected_preset_ = ExportPreset{};
        selected_index = -1;
    } else {
        // Keep the current selection by name when the file reloads
        // (user edits land between dialog opens); else index 0.
        for (int i = 0; i < static_cast<int>(presets_.size()); ++i) {
            if (presets_[i].name == selected_preset_.name) {
                selected_index = i;
                break;
            }
        }
        selected_preset_ = presets_[selected_index];
    }
    if (!children_attached_ || !preset_combo_) {
        return;
    }
    std::vector<XER::UI::Widgets::Combobox::Option> options;
    options.reserve(presets_.size());
    for (int i = 0; i < static_cast<int>(presets_.size()); ++i) {
        options.push_back({presets_[i].name, std::to_string(i)});
    }
    preset_combo_->SetOptions(std::move(options));
    // value = index string -> SetValue() displays the matching label.
    if (selected_index >= 0) {
        preset_combo_->SetValue(std::to_string(selected_index));
    }
    RefreshPresetUi();
}

void ExportDialog::RefreshPresetUi() {
    if (!children_attached_ || !preset_info_) {
        return;
    }
    preset_info_->SetText(DescribePreset(selected_preset_));
    Invalidate();
}

ExportOptions ExportDialog::GetExportOptions() const {
    ExportOptions options;
    ApplyPreset(selected_preset_, &options);
    switch (range_mode_) {
        case ExportRangeMode::WorkArea:
            // {0,0} — the engine resolves the work area itself.
            break;
        case ExportRangeMode::Custom: {
            const auto [in_s, out_s] = GetRange();
            options.range_in = in_s;
            options.range_out = out_s;
            break;
        }
        case ExportRangeMode::EntireSequence:
        default:
            // An explicit whole-project range — {0,0} in options would
            // mean "use the work area" (see ExportOptions).
            options.range_in = 0.0;
            options.range_out =
                project_ != nullptr ? project_->duration() : 0.0;
            break;
    }
    return options;
}

void ExportDialog::RefreshRangeUi() {
    if (!children_attached_) return;

    // Rebuild options only when work-area availability flips — the list
    // is what carries the "not set" suffix, and SetOptions() requests a
    // relayout so it must not run per progress tick.
    const int work_state = WorkAreaSet() ? 1 : 0;
    if (built_options_state_ != work_state) {
        built_options_state_ = work_state;
        range_radio_->SetOptions({
            "Entire sequence",
            work_state ? "Work area (in–out)"
                       : "Work area (in–out) — not set",
            "Custom",
        });
    }
    range_radio_->SetSelectedIndex(
        range_mode_ == ExportRangeMode::WorkArea    ? kRangeWorkArea
        : range_mode_ == ExportRangeMode::Custom    ? kRangeCustom
                                                  : kRangeEntire);

    SetRangeControlsEnabled(!running_ || finished_);

    // Estimated duration + resolution line.
    std::string info = "No project loaded";
    if (project_ != nullptr) {
        const auto [in_s, out_s] = GetRange();
        const double duration = project_->duration();
        const double span = out_s > in_s
                                ? std::min(out_s, duration) - in_s
                                : duration;
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
                      "%d × %d @ %s fps · est. duration %s",
                      project_->width, project_->height,
                      FormatFps(project_->fps_num, project_->fps_den).c_str(),
                      FormatDuration(std::max(0.0, span)).c_str());
        info = buffer;
    }
    info_label_->SetText(info);
    Invalidate();
}

void ExportDialog::Reset() {
    EnsureChildren();
    finished_ = false;
    running_ = false;
    progress_ = ExportProgress{};
    message_ = "Preparing export…";
    status_label_->SetText(message_);
    bar_->SetValue(0.0f);
    if (hooks_.on_export_start) {
        action_->SetText("Export");
        action_->SetStyle(XER::UI::Widgets::ButtonStyle::Primary);
    } else {
        action_->SetText("Cancel");
        action_->SetStyle(XER::UI::Widgets::ButtonStyle::Destructive);
    }
    RefreshRangeUi();
    Invalidate();
}

void ExportDialog::SetProgress(const ExportProgress& progress) {
    EnsureChildren();
    const bool was_running = running_;
    running_ = true;
    if (!was_running) {
        // Lock the range controls once the engine is live.
        SetRangeControlsEnabled(false);
        action_->SetText("Cancel");
        action_->SetStyle(XER::UI::Widgets::ButtonStyle::Destructive);
    }
    progress_ = progress;
    bar_->SetValue(static_cast<float>(progress.fraction));
    status_label_->SetText(progress.stage + "  " +
                           Percent(progress.fraction));
    Invalidate();
}

void ExportDialog::SetFinished(ExportStatus status,
                               const std::string& message) {
    EnsureChildren();
    finished_ = true;
    running_ = false;
    message_ = message.empty() ? "Export finished" : message;
    status_label_->SetText(message_);
    action_->SetText("Close");
    action_->SetStyle(status == ExportStatus::kDone
                          ? XER::UI::Widgets::ButtonStyle::Primary
                          : XER::UI::Widgets::ButtonStyle::Secondary);
    RefreshRangeUi();
    Invalidate();
}

void ExportDialog::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, {0.0f, 0.0f, 0.0f, 0.58f}, 0.0f);
    const float width = std::min(kPanelWidth, bounds.width - 40.0f);
    const XER::UI::Layout::Rect panel{
        bounds.x + (bounds.width - width) * 0.5f,
        bounds.y + (bounds.height - kPanelHeight) * 0.5f,
        width, kPanelHeight};
    renderer.FillRect(panel, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(panel, tokens.border, 1.0f, tokens.radius_large);
    renderer.FillRect({panel.x, panel.y, 4.0f, panel.height},
                      finished_ ? tokens.accent : tokens.focus,
                      tokens.radius_large);
    // Divider between the configuration sections (preset + range) and
    // the progress section.
    const float divider_y = panel.y + 304.0f;
    renderer.DrawLine({panel.x + 18.0f, divider_y},
                      {panel.x + panel.width - 18.0f, divider_y},
                      tokens.border, 1.0f);
}

void ExportDialog::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float width = std::min(kPanelWidth, bounds.width - 40.0f);
    const XER::UI::Layout::Rect panel{
        bounds.x + (bounds.width - width) * 0.5f,
        bounds.y + (bounds.height - kPanelHeight) * 0.5f,
        width, kPanelHeight};
    const float px = panel.x + 22.0f;
    const float pw = panel.width - 44.0f;
    title_->Layout({px, panel.y + 14.0f, pw, 26.0f}, tokens);
    // Preset section.
    preset_label_->Layout({px, panel.y + 44.0f, pw, 16.0f}, tokens);
    preset_combo_->Layout({px, panel.y + 62.0f, pw, 32.0f}, tokens);
    preset_info_->Layout({px, panel.y + 100.0f, pw, 18.0f}, tokens);
    // Range section.
    range_label_->Layout({px, panel.y + 128.0f, pw, 16.0f}, tokens);
    range_radio_->Layout({px, panel.y + 146.0f, pw, 84.0f}, tokens);
    const float field_y = panel.y + 236.0f;
    in_label_->Layout({px, field_y + 6.0f, 22.0f, 20.0f}, tokens);
    in_input_->Layout({px + 26.0f, field_y, 100.0f, 32.0f}, tokens);
    out_label_->Layout({px + 140.0f, field_y + 6.0f, 32.0f, 20.0f},
                       tokens);
    out_input_->Layout({px + 176.0f, field_y, 100.0f, 32.0f}, tokens);
    info_label_->Layout({px, panel.y + 274.0f, pw, 20.0f}, tokens);
    // Progress section (below the divider at +304).
    status_label_->Layout({px, panel.y + 314.0f, pw, 22.0f}, tokens);
    bar_->Layout({px, panel.y + 342.0f, pw, 14.0f}, tokens);
    action_->Layout({panel.x + panel.width - 118.0f,
                     panel.y + 368.0f, 94.0f, 34.0f}, tokens);
}

bool ExportDialog::HandleEvent(const XER::UI::Widgets::Event& event) {
    if (event.type == XER::UI::Widgets::Event::Type::KeyDown &&
        event.key == XER::UI::Widgets::KeyCode::Escape && finished_) {
        if (hooks_.on_cancel_or_close) hooks_.on_cancel_or_close();
        return true;
    }
    // Modal: consume pointer events outside the panel as well.
    return event.type == XER::UI::Widgets::Event::Type::MouseDown ||
           event.type == XER::UI::Widgets::Event::Type::MouseUp ||
           event.type == XER::UI::Widgets::Event::Type::Click;
}

void ExportDialog::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
