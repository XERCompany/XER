#include "project_settings_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "ve_theme.h"
#include "rendering/renderer.h"
#include "widgets/checkbox.h"
#include "widgets/label.h"
#include "widgets/scrub_number_field.h"
#include "widgets/select.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Tokens;

constexpr float kRowH = 30.0f;
constexpr float kGap = 8.0f;
constexpr float kLabelW = 92.0f;
constexpr float kSectionGap = 10.0f;

std::shared_ptr<W::ScrubNumberField> NumField(
    float value, float min, float max, const char* format,
    const char* unit = nullptr) {
    auto f = W::ScrubNumberField::Create();
    f->SetRange(min, max);
    f->SetStep(1.0f);
    f->SetAllowWheelScrub(false);
    f->SetValue(value);
    f->SetFormat(format);
    if (unit != nullptr) f->SetUnit(unit);
    return f;
}

constexpr int kSampleRates[] = {44100, 48000, 96000};
constexpr int kFpsPresets[] = {24, 25, 30, 50, 60};

}  // namespace

std::shared_ptr<ProjectSettingsDialog> ProjectSettingsDialog::Create() {
    return std::shared_ptr<ProjectSettingsDialog>(
        new ProjectSettingsDialog());
}

int ProjectSettingsDialog::ResolutionRowFor(int w, int h, int fps_num,
                                            int fps_den) const {
    const SequencePreset match = FindPreset(w, h, fps_num, fps_den);
    if (match.name.empty()) {
        return static_cast<int>(res_presets_.size());  // "Custom"
    }
    for (int i = 0; i < static_cast<int>(res_presets_.size()); ++i) {
        if (res_presets_[static_cast<std::size_t>(i)].name ==
            match.name) {
            return i;
        }
    }
    return static_cast<int>(res_presets_.size());
}

void ProjectSettingsDialog::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    name_input_ = std::make_shared<W::TextInput>();
    name_input_->SetPlaceholder("untitled");

    res_presets_ = BuiltinSequencePresets();
    std::vector<std::string> res_names;
    res_names.reserve(res_presets_.size() + 1);
    for (const SequencePreset& p : res_presets_) {
        res_names.push_back(p.name);
    }
    res_names.push_back("Custom");
    res_select_ = std::make_shared<W::Select>(res_names, 0);
    res_select_->SetOnChanged([this](int i) {
        if (i >= 0 && i < static_cast<int>(res_presets_.size())) {
            const SequencePreset& p =
                res_presets_[static_cast<std::size_t>(i)];
            width_field_->SetValue(static_cast<float>(p.w));
            height_field_->SetValue(static_cast<float>(p.h));
            fps_num_field_->SetValue(static_cast<float>(p.fps_num));
            fps_den_field_->SetValue(static_cast<float>(p.fps_den));
            // Reflect the preset's rate on the fps select too.
            int row = 5;  // Custom
            for (int k = 0; k < 5; ++k) {
                if (kFpsPresets[k] == p.fps_num && p.fps_den == 1) {
                    row = k;
                    break;
                }
            }
            fps_select_->SetSelectedIndex(row);
        }
        SyncCustomEnable();
    });
    width_field_ = NumField(1920.0f, 16.0f, 16384.0f, "%.0f", "px");
    height_field_ = NumField(1080.0f, 16.0f, 16384.0f, "%.0f", "px");

    std::vector<std::string> fps_names;
    for (int r : kFpsPresets) fps_names.push_back(std::to_string(r));
    fps_names.push_back("Custom");
    fps_select_ = std::make_shared<W::Select>(fps_names, 2);
    fps_select_->SetOnChanged([this](int i) {
        if (i >= 0 && i < 5) {
            fps_num_field_->SetValue(
                static_cast<float>(kFpsPresets[i]));
            fps_den_field_->SetValue(1.0f);
        }
        SyncCustomEnable();
    });
    fps_num_field_ = NumField(30.0f, 1.0f, 240.0f, "%.0f");
    fps_den_field_ = NumField(1.0f, 1.0f, 1001.0f, "%.0f");

    std::vector<std::string> rates;
    for (int r : kSampleRates) {
        rates.push_back(std::to_string(r) + " Hz");
    }
    rate_select_ = std::make_shared<W::Select>(rates, 1);

    proxies_check_ = std::make_shared<W::Checkbox>(true);
    ingest_check_ = std::make_shared<W::Checkbox>(false);
    proxy_dim_field_ = NumField(960.0f, 64.0f, 8192.0f, "%.0f", "px");
    proxy_crf_field_ = NumField(28.0f, 0.0f, 51.0f, "%.0f");
    proxy_cap_field_ = NumField(20480.0f, 0.0f, 1048576.0f, "%.0f",
                               "MiB");
    scratch_input_ = std::make_shared<W::TextInput>();
    scratch_input_->SetPlaceholder("system default");
    transcript_cap_field_ = NumField(0.0f, 0.0f, 1440.0f, "%.0f",
                                     "min");
    autosave_check_ = std::make_shared<W::Checkbox>(true);
    autosave_interval_field_ = NumField(120.0f, 5.0f, 3600.0f, "%.0f",
                                        "s");
    error_ = std::make_shared<W::Label>("", true);

    const std::vector<std::shared_ptr<W::Widget>> children = {
        name_input_,           res_select_,       width_field_,
        height_field_,         fps_select_,       fps_num_field_,
        fps_den_field_,        rate_select_,      proxies_check_,
        ingest_check_,         proxy_dim_field_,  proxy_crf_field_,
        proxy_cap_field_,      scratch_input_,    transcript_cap_field_,
        autosave_check_,       autosave_interval_field_, error_,
    };
    for (const auto& w : children) AddChild(w);
    SyncCustomEnable();
}

void ProjectSettingsDialog::SyncCustomEnable() {
    const bool res_custom =
        res_select_->GetSelectedIndex() ==
        static_cast<int>(res_presets_.size());
    width_field_->SetFocusable(res_custom);
    height_field_->SetFocusable(res_custom);
    const bool fps_custom = fps_select_->GetSelectedIndex() == 5;
    fps_num_field_->SetFocusable(fps_custom);
    fps_den_field_->SetFocusable(fps_custom);
}

void ProjectSettingsDialog::SetProject(const Project* project) {
    EnsureChildren();
    if (project == nullptr) return;
    width_field_->SetValue(static_cast<float>(project->width));
    height_field_->SetValue(static_cast<float>(project->height));
    fps_num_field_->SetValue(static_cast<float>(project->fps_num));
    fps_den_field_->SetValue(static_cast<float>(project->fps_den));
    res_select_->SetSelectedIndex(
        ResolutionRowFor(project->width, project->height,
                         project->fps_num, project->fps_den));
    int fps_row = 5;
    for (int i = 0; i < 5; ++i) {
        if (kFpsPresets[i] == project->fps_num &&
            project->fps_den == 1) {
            fps_row = i;
            break;
        }
    }
    fps_select_->SetSelectedIndex(fps_row);
    proxies_check_->SetChecked(project->use_proxies);
    ingest_check_->SetChecked(project->proxy_ingest);
    proxy_dim_field_->SetValue(static_cast<float>(project->proxy_max_dim));
    proxy_crf_field_->SetValue(static_cast<float>(project->proxy_crf));
    proxy_cap_field_->SetValue(
        static_cast<float>(project->proxy_cache_max_mb));
    SyncCustomEnable();
}

void ProjectSettingsDialog::SetSettings(
    const Ui2ProjectSettings& s) {
    EnsureChildren();
    name_input_->SetValue(s.display_name);
    width_field_->SetValue(static_cast<float>(s.width));
    height_field_->SetValue(static_cast<float>(s.height));
    fps_num_field_->SetValue(static_cast<float>(s.fps_num));
    fps_den_field_->SetValue(static_cast<float>(s.fps_den));
    res_select_->SetSelectedIndex(
        ResolutionRowFor(s.width, s.height, s.fps_num, s.fps_den));
    int fps_row = 5;
    for (int i = 0; i < 5; ++i) {
        if (kFpsPresets[i] == s.fps_num && s.fps_den == 1) {
            fps_row = i;
            break;
        }
    }
    fps_select_->SetSelectedIndex(fps_row);
    int rate_row = 1;
    for (int i = 0; i < 3; ++i) {
        if (kSampleRates[i] == s.audio_sample_rate) rate_row = i;
    }
    rate_select_->SetSelectedIndex(rate_row);
    proxies_check_->SetChecked(s.use_proxies);
    ingest_check_->SetChecked(s.proxy_ingest);
    proxy_dim_field_->SetValue(static_cast<float>(s.proxy_max_dim));
    proxy_crf_field_->SetValue(static_cast<float>(s.proxy_crf));
    proxy_cap_field_->SetValue(
        static_cast<float>(s.proxy_cache_max_mb));
    scratch_input_->SetValue(s.scratch_dir);
    transcript_cap_field_->SetValue(
        static_cast<float>(s.transcript_max_minutes));
    autosave_check_->SetChecked(s.autosave_enabled);
    autosave_interval_field_->SetValue(
        static_cast<float>(s.autosave_interval_s));
    SyncCustomEnable();
}

Ui2ProjectSettings ProjectSettingsDialog::GetSettings() const {
    Ui2ProjectSettings s;
    s.display_name = name_input_->GetValue();
    s.width =
        std::max(16, static_cast<int>(width_field_->GetValue()));
    s.height =
        std::max(16, static_cast<int>(height_field_->GetValue()));
    s.fps_num =
        std::max(1, static_cast<int>(fps_num_field_->GetValue()));
    s.fps_den =
        std::max(1, static_cast<int>(fps_den_field_->GetValue()));
    const int ri = rate_select_->GetSelectedIndex();
    s.audio_sample_rate =
        kSampleRates[std::clamp(ri, 0, 2)];
    s.use_proxies = proxies_check_->IsChecked();
    s.proxy_ingest = ingest_check_->IsChecked();
    s.proxy_max_dim =
        std::max(64, static_cast<int>(proxy_dim_field_->GetValue()));
    s.proxy_crf = std::clamp(
        static_cast<int>(proxy_crf_field_->GetValue()), 0, 51);
    s.proxy_cache_max_mb =
        std::max(0, static_cast<int>(proxy_cap_field_->GetValue()));
    s.scratch_dir = scratch_input_->GetValue();
    s.transcript_max_minutes = std::max(
        0.0, static_cast<double>(transcript_cap_field_->GetValue()));
    s.autosave_enabled = autosave_check_->IsChecked();
    s.autosave_interval_s = std::clamp(
        static_cast<int>(autosave_interval_field_->GetValue()), 5,
        3600);
    return s;
}

std::string ProjectSettingsDialog::Validate() {
    EnsureChildren();
    const Ui2ProjectSettings s = GetSettings();
    if (s.width <= 0 || s.height <= 0 || s.fps_num <= 0 ||
        s.fps_den <= 0) {
        SetError("Resolution and frame rate must be positive");
        return error_text_;
    }
    if (!s.scratch_dir.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(s.scratch_dir, ec) &&
            !std::filesystem::is_directory(s.scratch_dir, ec)) {
            SetError("Scratch path is not a directory");
            return error_text_;
        }
    }
    SetError({});
    return {};
}

void ProjectSettingsDialog::SetError(std::string text) {
    error_text_ = std::move(text);
    if (error_ != nullptr) error_->SetText(error_text_);
}

// ---- headless drive -------------------------------------------------

void ProjectSettingsDialog::SetName(const std::string& name) {
    EnsureChildren();
    name_input_->SetValue(name);
}
void ProjectSettingsDialog::SelectResolutionIndex(int index) {
    EnsureChildren();
    res_select_->SetSelectedIndex(index);
    if (index >= 0 &&
        index < static_cast<int>(res_presets_.size())) {
        const SequencePreset& p =
            res_presets_[static_cast<std::size_t>(index)];
        width_field_->SetValue(static_cast<float>(p.w));
        height_field_->SetValue(static_cast<float>(p.h));
        fps_num_field_->SetValue(static_cast<float>(p.fps_num));
        fps_den_field_->SetValue(static_cast<float>(p.fps_den));
    }
    SyncCustomEnable();
}
void ProjectSettingsDialog::SetWidthHeight(int width, int height) {
    EnsureChildren();
    res_select_->SetSelectedIndex(
        static_cast<int>(res_presets_.size()));   // Custom
    width_field_->SetValue(static_cast<float>(width));
    height_field_->SetValue(static_cast<float>(height));
    SyncCustomEnable();
}
void ProjectSettingsDialog::SelectFpsIndex(int index) {
    EnsureChildren();
    fps_select_->SetSelectedIndex(index);
    if (index >= 0 && index < 5) {
        fps_num_field_->SetValue(
            static_cast<float>(kFpsPresets[index]));
        fps_den_field_->SetValue(1.0f);
    }
    SyncCustomEnable();
}
void ProjectSettingsDialog::SetCustomFps(int fps_num, int fps_den) {
    EnsureChildren();
    fps_select_->SetSelectedIndex(5);
    fps_num_field_->SetValue(static_cast<float>(fps_num));
    fps_den_field_->SetValue(static_cast<float>(fps_den));
    SyncCustomEnable();
}
void ProjectSettingsDialog::SelectSampleRateIndex(int index) {
    EnsureChildren();
    rate_select_->SetSelectedIndex(index);
}
void ProjectSettingsDialog::SetUseProxies(bool on) {
    EnsureChildren();
    proxies_check_->SetChecked(on);
}
void ProjectSettingsDialog::SetProxyIngest(bool on) {
    EnsureChildren();
    ingest_check_->SetChecked(on);
}
void ProjectSettingsDialog::SetProxyMaxDim(int px) {
    EnsureChildren();
    proxy_dim_field_->SetValue(static_cast<float>(px));
}
void ProjectSettingsDialog::SetProxyCrf(int crf) {
    EnsureChildren();
    proxy_crf_field_->SetValue(static_cast<float>(crf));
}
void ProjectSettingsDialog::SetProxyCacheCap(int mib) {
    EnsureChildren();
    proxy_cap_field_->SetValue(static_cast<float>(mib));
}
void ProjectSettingsDialog::SetScratchDir(
    const std::string& utf8_dir) {
    EnsureChildren();
    scratch_input_->SetValue(utf8_dir);
}
void ProjectSettingsDialog::SetTranscriptCapMinutes(double minutes) {
    EnsureChildren();
    transcript_cap_field_->SetValue(static_cast<float>(minutes));
}
void ProjectSettingsDialog::SetAutosaveEnabled(bool on) {
    EnsureChildren();
    autosave_check_->SetChecked(on);
}
void ProjectSettingsDialog::SetAutosaveInterval(int seconds) {
    EnsureChildren();
    autosave_interval_field_->SetValue(static_cast<float>(seconds));
}

// ---- widget contract ------------------------------------------------

void ProjectSettingsDialog::Render(Rendering::Renderer& renderer,
                                   const Tokens& tokens) {
    EnsureChildren();
    const auto b = GetBounds();
    const auto section = [&](float y, const char* text) {
        renderer.DrawText(text, {b.x, y, b.width, 20.0f},
                          {tokens.muted, Theme::kFontCaption, 600});
    };
    const auto label = [&](float y, const char* text) {
        renderer.DrawText(text, {b.x + 8.0f, y + 7.0f, kLabelW, 20.0f},
                          {tokens.muted, Theme::kFontCaption, 500});
    };
    const auto caption = [&](float x, float y, const char* text) {
        renderer.DrawText(text, {x, y + 7.0f, 200.0f, 20.0f},
                          {tokens.foreground, Theme::kFontCaption,
                           500});
    };
    const float fx = b.x + kLabelW + 8.0f;
    const float fw = b.x + b.width - fx;
    float y = b.y + 4.0f;
    section(y, "SEQUENCE");
    y += 22.0f;
    label(y, "Name");
    y += kRowH + kGap;
    label(y, "Resolution");
    y += kRowH + kGap;
    label(y, "Frame rate");
    y += kRowH + kGap;
    label(y, "Audio rate");
    y += kRowH + kGap + kSectionGap;
    section(y, "MEDIA & CACHE");
    y += 22.0f;
    label(y, "Proxies");
    caption(fx + 26.0f, y, "decode via ready proxies");
    caption(fx + fw / 2.0f + 26.0f, y, "build on import");
    y += kRowH + kGap;
    label(y, "Proxy gen");
    y += kRowH + kGap;
    label(y, "Scratch dir");
    y += kRowH + kGap;
    label(y, "Transcript");
    caption(fx + (fw - kGap) / 2.0f + kGap + 4.0f, y,
            "minutes (0 = default)");
    y += kRowH + kGap;
    label(y, "Autosave");
    caption(fx + 26.0f, y, "every");
}

void ProjectSettingsDialog::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float x = bounds.x + kLabelW + 8.0f;
    const float w = bounds.x + bounds.width - x;
    const float half = (w - kGap) / 2.0f;
    const auto row = [&tokens](std::shared_ptr<W::Widget> c, float rx,
                               float ry, float rw,
                               float h = kRowH) {
        c->Layout({rx, ry, rw, h}, tokens);
    };
    float y = bounds.y + 4.0f + 22.0f;   // past "SEQUENCE"
    row(name_input_, x, y, w);
    y += kRowH + kGap;
    row(res_select_, x, y, half);
    row(width_field_, x + half + kGap, y, (half - kGap) / 2.0f);
    row(height_field_, x + half + kGap + (half - kGap) / 2.0f + kGap,
        y, (half - kGap) / 2.0f);
    y += kRowH + kGap;
    row(fps_select_, x, y, half);
    row(fps_num_field_, x + half + kGap, y, (half - kGap) / 2.0f);
    row(fps_den_field_, x + half + kGap + (half - kGap) / 2.0f + kGap,
        y, (half - kGap) / 2.0f);
    y += kRowH + kGap;
    row(rate_select_, x, y, w);
    y += kRowH + kGap + kSectionGap + 22.0f;   // past "MEDIA & CACHE"
    proxies_check_->Layout({x, y + 4.0f, 22.0f, 22.0f}, tokens);
    ingest_check_->Layout({x + w / 2.0f, y + 4.0f, 22.0f, 22.0f},
                          tokens);
    y += kRowH + kGap;
    const float third = (w - 2.0f * kGap) / 3.0f;
    row(proxy_dim_field_, x, y, third);
    row(proxy_crf_field_, x + third + kGap, y, third);
    row(proxy_cap_field_, x + 2.0f * (third + kGap), y, third);
    y += kRowH + kGap;
    row(scratch_input_, x, y, w);
    y += kRowH + kGap;
    row(transcript_cap_field_, x, y, half);
    y += kRowH + kGap;
    autosave_check_->Layout({x, y + 4.0f, 22.0f, 22.0f}, tokens);
    row(autosave_interval_field_, x + half + kGap, y, half - kGap);
    y += kRowH + kGap;
    error_->Layout({x, y, w, 18.0f}, tokens);
}

}  // namespace XER::VideoEditor::Ui2
