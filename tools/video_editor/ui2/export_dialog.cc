#include "export_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "timecode.h"
#include "rendering/renderer.h"
#include "ve_theme.h"
#include "widgets/button.h"
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
using XER::UI::Theme::Color;
using XER::UI::Theme::Tokens;

constexpr float kRowH = 30.0f;
constexpr float kGap = 8.0f;
constexpr float kLabelW = 88.0f;
constexpr float kSectionGap = 4.0f;

std::shared_ptr<W::ScrubNumberField> NumField(
    float value, float min, float max, const char* format,
    const char* unit = nullptr) {
    auto f = W::ScrubNumberField::Create();
    f->SetRange(min, max);
    f->SetStep(1.0f);
    f->SetSmallStep(0.1f);
    f->SetLargeStep(10.0f);
    f->SetAllowWheelScrub(false);
    f->SetValue(value);
    f->SetFormat(format);
    if (unit != nullptr) f->SetUnit(unit);
    return f;
}

std::string FormatBytes(double bytes) {
    char buf[64];
    if (bytes >= 1024.0 * 1024.0 * 1024.0) {
        std::snprintf(buf, sizeof(buf), "%.2f GB",
                      bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024.0 * 1024.0) {
        std::snprintf(buf, sizeof(buf), "%.1f MB",
                      bytes / (1024.0 * 1024.0));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
    }
    return buf;
}

std::string Basename(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

// Container table — the only codec/container pairings the FFmpeg
// streaming encoder is exercised with (export_engine.cc muxes by
// output-path extension; NVENC names resolve through
// GpuVideo::ResolveVideoCodecName with software fallback).
const std::vector<ExportDialog::FormatSpec>&
ExportDialog::FormatSpecs() {
    static const std::vector<FormatSpec> specs = {
        {"MP4 (.mp4)", "mp4",
         {"H.264 (libx264)", "H.264 NVENC", "H.265 (libx265)"},
         {"libx264", "h264_nvenc", "libx265"},
         {"AAC", "MP3"},
         {"aac", "mp3"}},
        {"MOV (.mov)", "mov",
         {"H.264 (libx264)", "H.264 NVENC", "H.265 (libx265)"},
         {"libx264", "h264_nvenc", "libx265"},
         {"AAC", "MP3"},
         {"aac", "mp3"}},
        {"WebM (.webm)", "webm",
         {"VP9 (libvpx-vp9)"},
         {"libvpx-vp9"},
         {"Opus (libopus)", "Vorbis"},
         {"libopus", "libvorbis"}},
    };
    return specs;
}

int ExportDialog::FormatIndex() const {
    const int i = format_select_->GetSelectedIndex();
    return std::clamp(i, 0, static_cast<int>(FormatSpecs().size()) - 1);
}

const ExportPreset& ExportDialog::PresetAt(int index) const {
    return presets_.at(static_cast<std::size_t>(
        std::clamp(index, 0, static_cast<int>(presets_.size()) - 1)));
}

std::shared_ptr<ExportDialog> ExportDialog::Create() {
    // Shared-from-private ctor pattern matches the kit factories.
    return std::shared_ptr<ExportDialog>(new ExportDialog());
}

void ExportDialog::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    estimate_ = std::make_shared<W::Label>("", true);
    error_ = std::make_shared<W::Label>("", true);

    RebuildPresetList();
    std::vector<std::string> preset_names;
    preset_names.reserve(presets_.size() + 1);
    for (const ExportPreset& p : presets_) preset_names.push_back(p.name);
    preset_names.push_back("Custom");
    preset_select_ = std::make_shared<W::Select>(preset_names, 1);
    preset_select_->SetOnChanged([this](int i) {
        if (i >= 0 && i < static_cast<int>(presets_.size())) {
            ApplyPreset(presets_[static_cast<std::size_t>(i)]);
        }
        RefreshEstimate();
    });

    std::vector<std::string> formats;
    for (const FormatSpec& f : FormatSpecs()) formats.push_back(f.label);
    format_select_ = std::make_shared<W::Select>(formats, 0);
    format_select_->SetOnChanged([this](int) {
        SyncCodecOptions();
        // Keep the picked extension on the output path when one is
        // already typed.
        output_input_->SetValue(PathWithExtension(
            output_input_->GetValue(),
            FormatSpecs()[static_cast<std::size_t>(FormatIndex())]
                .extension));
        RefreshEstimate();
    });

    resolution_select_ = std::make_shared<W::Select>(
        std::vector<std::string>{"Match source", "3840 x 2160",
                                 "1920 x 1080", "1280 x 720",
                                 "960 x 540", "Custom"},
        0);
    resolution_select_->SetOnChanged(
        [this](int) { SyncResolutionEnable(); RefreshEstimate(); });
    width_field_ = NumField(0.0f, 2.0f, 16384.0f, "%.0f", "px");
    height_field_ = NumField(0.0f, 2.0f, 16384.0f, "%.0f", "px");
    width_field_->SetOnValueChange(
        [this](float) { RefreshEstimate(); });
    height_field_->SetOnValueChange(
        [this](float) { RefreshEstimate(); });

    fps_select_ = std::make_shared<W::Select>(
        std::vector<std::string>{"Project rate", "24", "25", "30",
                                 "50", "60", "Custom"},
        0);
    fps_select_->SetOnChanged([this](int) { SyncResolutionEnable(); });
    fps_num_field_ = NumField(30.0f, 1.0f, 240.0f, "%.0f");
    fps_den_field_ = NumField(1.0f, 1.0f, 1001.0f, "%.0f");

    vcodec_select_ = std::make_shared<W::Select>();
    vcodec_select_->SetOnChanged(
        [this](int) { RefreshEstimate(); });
    audio_only_check_ = std::make_shared<W::Checkbox>(false);
    audio_only_check_->SetOnChanged(
        [this](bool) { SyncCodecOptions(); RefreshEstimate(); });

    rate_mode_select_ = std::make_shared<W::Select>(
        std::vector<std::string>{"Bitrate", "CRF"}, 0);
    rate_mode_select_->SetOnChanged(
        [this](int) { SyncRateMode(); RefreshEstimate(); });
    rate_field_ = NumField(8.0f, 0.1f, 200.0f, "%.4g");
    rate_field_->SetOnValueChange(
        [this](float) { RefreshEstimate(); });
    rate_unit_ = std::make_shared<W::Label>("Mbps", true);

    acodec_select_ = std::make_shared<W::Select>();
    acodec_select_->SetOnChanged(
        [this](int) { RefreshEstimate(); });
    akbps_field_ = NumField(192.0f, 8.0f, 1024.0f, "%.0f", "kbps");
    akbps_field_->SetOnValueChange(
        [this](float) { RefreshEstimate(); });

    range_select_ = std::make_shared<W::Select>(
        std::vector<std::string>{"Entire sequence", "Work area",
                                 "Custom in/out"},
        0);
    range_select_->SetOnChanged(
        [this](int) { SyncRangeEnable(); RefreshEstimate(); });
    range_in_field_ = NumField(0.0f, 0.0f, 86400.0f, "%.3f", "s");
    range_out_field_ = NumField(0.0f, 0.0f, 86400.0f, "%.3f", "s");
    range_in_field_->SetOnValueChange(
        [this](float) { RefreshEstimate(); });
    range_out_field_->SetOnValueChange(
        [this](float) { RefreshEstimate(); });

    output_input_ = std::make_shared<W::TextInput>();
    output_input_->SetPlaceholder("output path");
    output_input_->SetOnValueChanged(
        [this](const std::string&) { RefreshEstimate(); });
    browse_button_ = std::make_shared<W::Button>(
        "Browse…", W::ButtonStyle::Secondary);
    browse_button_->SetOnClick([this] { Browse(); });

    const std::vector<std::shared_ptr<W::Widget>> children = {
        estimate_,      error_,           preset_select_,
        format_select_, resolution_select_, width_field_,
        height_field_,  fps_select_,      fps_num_field_,
        fps_den_field_, vcodec_select_,   audio_only_check_,
        rate_mode_select_, rate_field_,   rate_unit_,
        acodec_select_, akbps_field_,     range_select_,
        range_in_field_, range_out_field_, output_input_,
        browse_button_,
    };
    for (const auto& w : children) {
        AddChild(w);
    }
    SyncCodecOptions();
    SyncRateMode();
    SyncResolutionEnable();
    SyncRangeEnable();
    RefreshEstimate();
}

void ExportDialog::RebuildPresetList() {
    presets_ = BuiltinPresets();
    std::vector<ExportPreset> user;
    std::string error;
    // Tolerant load: a missing file yields an empty list; malformed
    // entries are skipped inside LoadPresets itself.
    if (LoadPresets(DefaultPresetsPath(), &user, &error)) {
        for (const ExportPreset& p : user) {
            presets_.push_back(p);   // index-unique, may shadow names
        }
    }
}

void ExportDialog::SetProject(const Project* project) {
    EnsureChildren();
    project_ = project;
    if (project_ != nullptr) {
        width_field_->SetValue(static_cast<float>(project_->width));
        height_field_->SetValue(static_cast<float>(project_->height));
        fps_num_field_->SetValue(
            static_cast<float>(project_->fps_num));
        fps_den_field_->SetValue(
            static_cast<float>(project_->fps_den));
        range_out_field_->SetValue(
            static_cast<float>(project_->duration()));
        if (output_input_->GetValue().empty()) {
            std::string stem = "sequence";
            // The shell feeds the project basename via SetRequest or
            // a later SetOutputPath — here a neutral default.
            output_input_->SetValue(stem + ".mp4");
        }
    }
    SyncResolutionEnable();
    SyncRangeEnable();
    RefreshEstimate();
}

void ExportDialog::SetRequest(const ExportRequest& r) {
    EnsureChildren();
    for (int i = 0; i < static_cast<int>(FormatSpecs().size()); ++i) {
        if (FormatSpecs()[static_cast<std::size_t>(i)].extension ==
            r.container) {
            format_select_->SetSelectedIndex(i);
            break;
        }
    }
    SyncCodecOptions();
    const FormatSpec& spec =
        FormatSpecs()[static_cast<std::size_t>(FormatIndex())];
    for (int i = 0;
         i < static_cast<int>(spec.video_names.size()); ++i) {
        if (spec.video_names[static_cast<std::size_t>(i)] ==
            r.video_codec) {
            vcodec_select_->SetSelectedIndex(i);
            break;
        }
    }
    audio_only_check_->SetChecked(r.video_codec.empty());
    for (int i = 0;
         i < static_cast<int>(spec.audio_names.size()); ++i) {
        if (spec.audio_names[static_cast<std::size_t>(i)] ==
            r.audio_codec) {
            acodec_select_->SetSelectedIndex(i);
            break;
        }
    }
    if (r.width > 0 && r.height > 0) {
        width_field_->SetValue(static_cast<float>(r.width));
        height_field_->SetValue(static_cast<float>(r.height));
        // Match a known preset row when the pair lines up.
        static const int kSizes[][2] = {
            {3840, 2160}, {1920, 1080}, {1280, 720}, {960, 540}};
        int row = 5;  // Custom
        for (int i = 0; i < 4; ++i) {
            if (kSizes[i][0] == r.width && kSizes[i][1] == r.height) {
                row = i + 1;
                break;
            }
        }
        resolution_select_->SetSelectedIndex(row);
    } else {
        resolution_select_->SetSelectedIndex(0);
    }
    if (r.fps_num > 0) {
        static const int kFps[] = {24, 25, 30, 50, 60};
        int row = 6;  // Custom
        if (r.fps_den == 1) {
            for (int i = 0; i < 5; ++i) {
                if (kFps[i] == r.fps_num) row = i + 1;
            }
        }
        fps_select_->SetSelectedIndex(row);
        fps_num_field_->SetValue(static_cast<float>(r.fps_num));
        fps_den_field_->SetValue(static_cast<float>(r.fps_den));
    } else {
        fps_select_->SetSelectedIndex(0);
    }
    rate_mode_select_->SetSelectedIndex(r.video_crf >= 0 ? 1 : 0);
    SyncRateMode();
    rate_field_->SetValue(
        r.video_crf >= 0 ? static_cast<float>(r.video_crf)
                         : static_cast<float>(r.video_bit_rate) /
                               1000000.0f);
    akbps_field_->SetValue(
        static_cast<float>(r.audio_bit_rate) / 1000.0f);
    range_select_->SetSelectedIndex(static_cast<int>(r.range));
    range_in_field_->SetValue(static_cast<float>(r.range_in));
    range_out_field_->SetValue(static_cast<float>(r.range_out));
    if (!r.output_path.empty()) output_input_->SetValue(r.output_path);
    SyncResolutionEnable();
    SyncRangeEnable();
    RefreshEstimate();
}

void ExportDialog::SyncCodecOptions() {
    const FormatSpec& spec =
        FormatSpecs()[static_cast<std::size_t>(FormatIndex())];
    const std::string prev_v =
        vcodec_select_ != nullptr &&
                vcodec_select_->GetSelectedIndex() >= 0 &&
                vcodec_select_->GetSelectedIndex() <
                        static_cast<int>(spec.video_names.size())
            ? spec.video_names[static_cast<std::size_t>(
                  vcodec_select_->GetSelectedIndex())]
            : std::string{};
    vcodec_select_->SetOptions(spec.video_codecs);
    int vsel = 0;
    if (!prev_v.empty()) {
        for (int i = 0;
             i < static_cast<int>(spec.video_names.size()); ++i) {
            if (spec.video_names[static_cast<std::size_t>(i)] ==
                prev_v) {
                vsel = i;
                break;
            }
        }
    }
    vcodec_select_->SetSelectedIndex(vsel);
    acodec_select_->SetOptions(spec.audio_codecs);
    acodec_select_->SetSelectedIndex(0);
}

void ExportDialog::SyncRateMode() {
    const bool crf = rate_mode_select_->GetSelectedIndex() == 1;
    rate_unit_->SetText(crf ? "CRF" : "Mbps");
    rate_field_->SetFormat(crf ? "%.0f" : "%.4g");
    rate_field_->SetRange(crf ? 0.0f : 0.1f, crf ? 51.0f : 200.0f);
    rate_field_->SetUnit(crf ? "" : "Mbps");
    if (crf && rate_field_->GetValue() > 51.0f) {
        rate_field_->SetValue(23.0f);
    }
    if (!crf && rate_field_->GetValue() < 0.1f) {
        rate_field_->SetValue(8.0f);
    }
}

void ExportDialog::SyncResolutionEnable() {
    const bool custom =
        resolution_select_->GetSelectedIndex() == 5;   // "Custom"
    width_field_->SetFocusable(custom);
    height_field_->SetFocusable(custom);
    const bool fps_custom = fps_select_->GetSelectedIndex() == 6;
    fps_num_field_->SetFocusable(fps_custom);
    fps_den_field_->SetFocusable(fps_custom);
}

void ExportDialog::SyncRangeEnable() {
    const bool custom = range_select_->GetSelectedIndex() == 2;
    range_in_field_->SetFocusable(custom);
    range_out_field_->SetFocusable(custom);
}

void ExportDialog::ApplyPreset(const ExportPreset& preset) {
    // Codec names are container-bound — apply the preset's codec only
    // when the current container carries it; otherwise keep the
    // container default. bitrate/scale always apply.
    const FormatSpec& spec =
        FormatSpecs()[static_cast<std::size_t>(FormatIndex())];
    if (!preset.video_codec.empty()) {
        for (int i = 0;
             i < static_cast<int>(spec.video_names.size()); ++i) {
            if (spec.video_names[static_cast<std::size_t>(i)] ==
                preset.video_codec) {
                vcodec_select_->SetSelectedIndex(i);
                break;
            }
        }
        audio_only_check_->SetChecked(false);
    } else {
        audio_only_check_->SetChecked(true);
    }
    for (int i = 0;
         i < static_cast<int>(spec.audio_names.size()); ++i) {
        if (spec.audio_names[static_cast<std::size_t>(i)] ==
            preset.audio_codec) {
            acodec_select_->SetSelectedIndex(i);
            break;
        }
    }
    rate_mode_select_->SetSelectedIndex(0);   // presets carry bitrate
    SyncRateMode();
    rate_field_->SetValue(static_cast<float>(preset.video_bit_rate) /
                          1000000.0f);
    akbps_field_->SetValue(static_cast<float>(preset.audio_bit_rate) /
                           1000.0f);
    if (preset.resolution_scale < 0.999 && project_ != nullptr) {
        resolution_select_->SetSelectedIndex(5);   // Custom
        width_field_->SetValue(static_cast<float>(std::lround(
            project_->width * preset.resolution_scale)));
        height_field_->SetValue(static_cast<float>(std::lround(
            project_->height * preset.resolution_scale)));
        SyncResolutionEnable();
    }
}

void ExportDialog::SetError(std::string text) {
    error_text_ = std::move(text);
    if (error_ != nullptr) error_->SetText(error_text_);
}

std::string ExportDialog::PathWithExtension(
    const std::string& path, const std::string& extension) const {
    if (path.empty()) return path;
    const std::size_t slash = path.find_last_of("/\\");
    const std::string dir =
        slash == std::string::npos ? std::string{}
                                   : path.substr(0, slash + 1);
    std::string base = Basename(path);
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base.resize(dot);
    return dir + base + "." + extension;
}

void ExportDialog::RefreshEstimate() {
    if (estimate_ == nullptr) return;
    const double bytes = EstimatedBytes();
    const auto [in_s, out_s] = ResolvedRangeSeconds();
    std::string text = "~" + FormatBytes(bytes) + " estimated";
    if (project_ != nullptr) {
        text += " · " + FormatTimecode(in_s, project_->fps_num,
                                      project_->fps_den) +
                "–" +
                FormatTimecode(out_s, project_->fps_num,
                               project_->fps_den);
    }
    estimate_->SetText(text);
}

std::pair<double, double> ExportDialog::ResolvedRangeSeconds() const {
    const double duration = project_ != nullptr ? project_->duration()
                                                : 0.0;
    switch (static_cast<ExportRequest::Range>(
        range_select_->GetSelectedIndex())) {
        case ExportRequest::Range::WorkArea:
            if (project_ != nullptr &&
                project_->work_out > project_->work_in) {
                return {project_->work_in, project_->work_out};
            }
            return {0.0, duration};
        case ExportRequest::Range::Custom: {
            const double in_s =
                std::clamp<double>(range_in_field_->GetValue(), 0.0,
                                   duration);
            const double out_s =
                std::clamp<double>(range_out_field_->GetValue(), 0.0,
                                   duration);
            return {in_s, out_s};
        }
        case ExportRequest::Range::WholeTimeline:
        default:
            return {0.0, duration};
    }
}

double ExportDialog::EstimatedBytes() const {
    const auto [in_s, out_s] = ResolvedRangeSeconds();
    const double seconds = std::max(0.0, out_s - in_s);
    const bool audio_only = audio_only_check_->IsChecked();
    const double vrate =
        audio_only ? 0.0
                   : (rate_mode_select_->GetSelectedIndex() == 1
                          // CRF has no fixed rate — use the field's
                          // nominal Mbps guess so the estimate stays
                          // honest-ish (and flags it as approximate).
                          ? static_cast<double>(rate_field_->GetValue())
                          : static_cast<double>(rate_field_->GetValue()));
    const double arate = akbps_field_->GetValue() / 1000.0;  // -> Mbps
    return (vrate + arate) * 1.0e6 * seconds / 8.0;
}

std::string ExportDialog::FormatSummary() const {
    const FormatSpec& spec =
        FormatSpecs()[static_cast<std::size_t>(FormatIndex())];
    if (audio_only_check_->IsChecked()) {
        return "Audio only " + spec.extension;
    }
    const int vi = std::clamp(vcodec_select_->GetSelectedIndex(), 0,
                              static_cast<int>(spec.video_codecs.size()) - 1);
    std::string codec_label = spec.video_codecs[static_cast<std::size_t>(vi)];
    const std::size_t paren = codec_label.find(" (");
    if (paren != std::string::npos) codec_label.resize(paren);
    const std::string container = spec.label.substr(0, spec.label.find(' '));
    if (rate_mode_select_->GetSelectedIndex() == 1) {
        return codec_label + " " + container + " · CRF " +
               std::to_string(
                   static_cast<int>(rate_field_->GetValue()));
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.4g Mbps",
                  static_cast<double>(rate_field_->GetValue()));
    return codec_label + " " + container + " · " + buf;
}

std::string ExportDialog::RangeSummary() const {
    const auto mode = static_cast<ExportRequest::Range>(
        range_select_->GetSelectedIndex());
    const auto [in_s, out_s] = ResolvedRangeSeconds();
    const int fps_num = project_ != nullptr ? project_->fps_num : 30;
    const int fps_den = project_ != nullptr ? project_->fps_den : 1;
    const auto tc = [&](double t) {
        return FormatTimecode(t, fps_num, fps_den);
    };
    switch (mode) {
        case ExportRequest::Range::WorkArea:
            return "work area " + tc(in_s) + "–" + tc(out_s);
        case ExportRequest::Range::Custom:
            return "custom " + tc(in_s) + "–" + tc(out_s);
        case ExportRequest::Range::WholeTimeline:
        default:
            return "entire sequence";
    }
}

ExportRequest ExportDialog::GetRequest() const {
    ExportRequest r;
    const FormatSpec& spec =
        FormatSpecs()[static_cast<std::size_t>(FormatIndex())];
    r.container = spec.extension;
    if (audio_only_check_->IsChecked()) {
        r.video_codec.clear();
    } else {
        const int vi =
            std::clamp(vcodec_select_->GetSelectedIndex(), 0,
                       static_cast<int>(spec.video_names.size()) - 1);
        r.video_codec =
            spec.video_names[static_cast<std::size_t>(vi)];
    }
    const int ai =
        std::clamp(acodec_select_->GetSelectedIndex(), 0,
                   static_cast<int>(spec.audio_names.size()) - 1);
    r.audio_codec = spec.audio_names[static_cast<std::size_t>(ai)];

    const int res = resolution_select_->GetSelectedIndex();
    static const int kSizes[][2] = {
        {3840, 2160}, {1920, 1080}, {1280, 720}, {960, 540}};
    if (res >= 1 && res <= 4) {
        r.width = kSizes[res - 1][0];
        r.height = kSizes[res - 1][1];
    } else if (res == 5) {
        r.width = std::max(2, static_cast<int>(width_field_->GetValue()));
        r.height =
            std::max(2, static_cast<int>(height_field_->GetValue()));
    }
    const int fps_sel = fps_select_->GetSelectedIndex();
    static const int kFps[] = {24, 25, 30, 50, 60};
    if (fps_sel >= 1 && fps_sel <= 5) {
        r.fps_num = kFps[fps_sel - 1];
        r.fps_den = 1;
    } else if (fps_sel == 6) {
        r.fps_num =
            std::max(1, static_cast<int>(fps_num_field_->GetValue()));
        r.fps_den =
            std::max(1, static_cast<int>(fps_den_field_->GetValue()));
    }
    if (rate_mode_select_->GetSelectedIndex() == 1) {
        r.video_crf = std::clamp(
            static_cast<int>(rate_field_->GetValue()), 0, 51);
    } else {
        r.video_bit_rate = static_cast<int>(
            std::lround(rate_field_->GetValue() * 1000000.0));
    }
    r.audio_bit_rate = static_cast<int>(
        std::lround(akbps_field_->GetValue() * 1000.0));
    r.range = static_cast<ExportRequest::Range>(
        range_select_->GetSelectedIndex());
    r.range_in = range_in_field_->GetValue();
    r.range_out = range_out_field_->GetValue();
    r.output_path = output_input_->GetValue();
    const int pi = preset_select_->GetSelectedIndex();
    if (pi >= 0 && pi < static_cast<int>(presets_.size())) {
        r.preset_name = presets_[static_cast<std::size_t>(pi)].name;
    }
    return r;
}

std::string ExportDialog::Validate() {
    EnsureChildren();
    const ExportRequest r = GetRequest();
    if (r.video_codec.empty() && r.audio_codec.empty()) {
        SetError("audio-only export still needs an audio codec");
        return error_text_;
    }
    if (r.output_path.empty()) {
        SetError("Choose an output path");
        return error_text_;
    }
    if (r.video_crf >= 0 &&
        (r.video_codec == "libvpx-vp9" || r.video_codec.empty())) {
        // libvpx honors a different quality knob — keep the honest
        // message rather than silently encoding at a default.
        SetError("CRF rate control is unsupported for " +
                 (r.video_codec.empty() ? std::string("audio-only")
                                        : r.video_codec));
        return error_text_;
    }
    if (r.range == ExportRequest::Range::WorkArea &&
        project_ != nullptr && project_->work_out <= project_->work_in) {
        SetError("No work area set — mark I/O on the program monitor");
        return error_text_;
    }
    if (r.range == ExportRequest::Range::Custom) {
        const double duration =
            project_ != nullptr ? project_->duration() : 0.0;
        if (r.range_out <= r.range_in || r.range_in < 0.0 ||
            r.range_out > duration + 1e-6) {
            SetError("Custom range outside the timeline");
            return error_text_;
        }
    }
    if (project_ != nullptr && project_->duration() <= 0.0 &&
        r.range != ExportRequest::Range::Custom) {
        SetError("Timeline is empty — nothing to export");
        return error_text_;
    }
    SetError({});
    return {};
}

void ExportDialog::SetBrowseHandler(
    std::function<std::string(const std::string&,
                              const std::string&)>
        handler) {
    browse_ = std::move(handler);
}

void ExportDialog::Browse() {
    EnsureChildren();
    if (browse_ == nullptr) return;
    const FormatSpec& spec =
        FormatSpecs()[static_cast<std::size_t>(FormatIndex())];
    const std::string picked =
        browse_(spec.extension, output_input_->GetValue());
    if (!picked.empty()) {
        output_input_->SetValue(
            PathWithExtension(picked, spec.extension));
        RefreshEstimate();
    }
}

// ---- headless drive ------------------------------------------------

void ExportDialog::SelectFormatIndex(int index) {
    EnsureChildren();
    format_select_->SetSelectedIndex(index);
    SyncCodecOptions();
    RefreshEstimate();
}
void ExportDialog::SelectPresetIndex(int index) {
    EnsureChildren();
    preset_select_->SetSelectedIndex(index);
    if (index >= 0 && index < static_cast<int>(presets_.size())) {
        ApplyPreset(presets_[static_cast<std::size_t>(index)]);
    }
    RefreshEstimate();
}
void ExportDialog::SelectResolutionIndex(int index) {
    EnsureChildren();
    resolution_select_->SetSelectedIndex(index);
    SyncResolutionEnable();
    RefreshEstimate();
}
void ExportDialog::SelectFpsIndex(int index) {
    EnsureChildren();
    fps_select_->SetSelectedIndex(index);
    SyncResolutionEnable();
}
void ExportDialog::SelectVideoCodecIndex(int index) {
    EnsureChildren();
    vcodec_select_->SetSelectedIndex(index);
    RefreshEstimate();
}
void ExportDialog::SelectAudioCodecIndex(int index) {
    EnsureChildren();
    acodec_select_->SetSelectedIndex(index);
    RefreshEstimate();
}
void ExportDialog::SelectRateModeIndex(int index) {
    EnsureChildren();
    rate_mode_select_->SetSelectedIndex(index);
    SyncRateMode();
    RefreshEstimate();
}
void ExportDialog::SelectRangeIndex(int index) {
    EnsureChildren();
    range_select_->SetSelectedIndex(index);
    SyncRangeEnable();
    RefreshEstimate();
}
void ExportDialog::SetWidthHeight(int width, int height) {
    EnsureChildren();
    width_field_->SetValue(static_cast<float>(width));
    height_field_->SetValue(static_cast<float>(height));
    RefreshEstimate();
}
void ExportDialog::SetCustomFps(int fps_num, int fps_den) {
    EnsureChildren();
    fps_select_->SetSelectedIndex(6);
    fps_num_field_->SetValue(static_cast<float>(fps_num));
    fps_den_field_->SetValue(static_cast<float>(fps_den));
    SyncResolutionEnable();
}
void ExportDialog::SetRateValue(float value) {
    EnsureChildren();
    rate_field_->SetValue(value);
    RefreshEstimate();
}
void ExportDialog::SetAudioKbps(float kbps) {
    EnsureChildren();
    akbps_field_->SetValue(kbps);
    RefreshEstimate();
}
void ExportDialog::SetCustomRange(double in_seconds,
                                  double out_seconds) {
    EnsureChildren();
    range_select_->SetSelectedIndex(2);
    range_in_field_->SetValue(static_cast<float>(in_seconds));
    range_out_field_->SetValue(static_cast<float>(out_seconds));
    SyncRangeEnable();
    RefreshEstimate();
}
void ExportDialog::SetOutputPath(const std::string& utf8_path) {
    EnsureChildren();
    output_input_->SetValue(utf8_path);
    RefreshEstimate();
}

// ---- widget contract -----------------------------------------------

void ExportDialog::Render(Rendering::Renderer& renderer,
                          const Tokens& tokens) {
    EnsureChildren();
    const auto b = GetBounds();
    const auto label = [&](float y, const char* text) {
        renderer.DrawText(text, {b.x, y + 7.0f, kLabelW, 20.0f},
                          {tokens.muted, Theme::kFontCaption, 500});
    };
    // Row anchors mirror Layout() — keep in sync.
    float y = b.y + 4.0f;
    label(y, "Preset");
    y += kRowH + kGap;
    label(y, "Format");
    y += kRowH + kGap;
    label(y, "Resolution");
    y += kRowH + kGap;
    label(y, "Frame rate");
    y += kRowH + kGap;
    label(y, "Video");
    // Caption next to the audio-only checkbox (right-aligned tail of
    // the video row — Layout() parks the box at x+w-96).
    const float x = b.x + kLabelW;
    const float w = b.x + b.width - x;
    renderer.DrawText("audio only",
                      {x + w - 74.0f, y + 8.0f, 70.0f, 18.0f},
                      {tokens.muted, Theme::kFontCaption, 500});
    y += kRowH + kGap;
    label(y, "Rate");
    y += kRowH + kGap;
    label(y, "Audio");
    y += kRowH + kGap;
    label(y, "Range");
    y += kRowH + kGap;
    label(y, "Output");
}

void ExportDialog::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float x = bounds.x + kLabelW;
    const float w = bounds.x + bounds.width - x;
    const float half = (w - kGap) / 2.0f;
    float y = bounds.y + 4.0f;
    const auto row = [&y, &tokens](std::shared_ptr<W::Widget> c,
                                   float rx, float rw,
                                   float h = kRowH) {
        c->Layout({rx, y, rw, h}, tokens);
    };

    row(preset_select_, x, w);
    y += kRowH + kGap;
    row(format_select_, x, w);
    y += kRowH + kGap;
    row(resolution_select_, x, half);
    row(width_field_, x + half + kGap, (half - kGap) / 2.0f);
    row(height_field_, x + half + kGap + (half - kGap) / 2.0f + kGap,
        (half - kGap) / 2.0f);
    y += kRowH + kGap;
    row(fps_select_, x, half);
    row(fps_num_field_, x + half + kGap, (half - kGap) / 2.0f);
    row(fps_den_field_, x + half + kGap + (half - kGap) / 2.0f + kGap,
        (half - kGap) / 2.0f);
    y += kRowH + kGap;
    row(vcodec_select_, x, w - 104.0f);
    audio_only_check_->Layout({x + w - 96.0f, y, 22.0f, 22.0f},
                              tokens);
    // "audio only" caption beside the checkbox — drawn in Render.
    y += kRowH + kGap;
    row(rate_mode_select_, x, 92.0f);
    row(rate_field_, x + 100.0f, w - 100.0f - 46.0f);
    rate_unit_->Layout({x + w - 42.0f, y + 6.0f, 42.0f, 20.0f},
                       tokens);
    y += kRowH + kGap;
    row(acodec_select_, x, half);
    row(akbps_field_, x + half + kGap, half - kGap);
    y += kRowH + kGap;
    row(range_select_, x, half);
    row(range_in_field_, x + half + kGap, (half - kGap) / 2.0f);
    row(range_out_field_, x + half + kGap + (half - kGap) / 2.0f + kGap,
        (half - kGap) / 2.0f);
    y += kRowH + kGap;
    row(output_input_, x, w - 84.0f);
    row(browse_button_, x + w - 76.0f, 76.0f);
    y += kRowH + kGap + kSectionGap;
    estimate_->Layout({x, y, w, 18.0f}, tokens);
    y += 20.0f;
    error_->Layout({x, y, w, 18.0f}, tokens);
}

}  // namespace XER::VideoEditor::Ui2
