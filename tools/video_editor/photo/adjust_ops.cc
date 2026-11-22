// adjust_ops.cc — see adjust_ops.h.
#include "photo/adjust_ops.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::Photo {

namespace {

using P = AdjustmentParam;
using R = std::pair<double, double>;

AdjustmentOpInfo Op(const char* name, const char* label,
                    std::vector<P> defaults,
                    std::vector<R> ranges) {
    AdjustmentOpInfo info;
    info.name = name;
    info.label = label;
    info.defaults = std::move(defaults);
    info.ranges = std::move(ranges);
    return info;
}

const std::vector<AdjustmentOpInfo>& Ops() {
    static const std::vector<AdjustmentOpInfo> kOps = {
        Op("brightness_contrast", "Brightness/Contrast",
           {{"brightness", 0.0}, {"contrast", 0.0}},
           {{-255, 255}, {-255, 255}}),
        Op("hsl", "Hue/Saturation",
           {{"hue", 0.0}, {"saturation", 0.0}, {"lightness", 0.0}},
           {{-180, 180}, {-100, 100}, {-100, 100}}),
        Op("exposure", "Exposure",
           {{"exposure", 0.0}, {"gamma", 1.0}},
           {{-10, 10}, {0.1, 10}}),
        Op("levels", "Levels",
           {{"in_black", 0.0}, {"in_white", 255.0}, {"gamma", 1.0},
            {"out_black", 0.0}, {"out_white", 255.0}},
           {{0, 255}, {0, 255}, {0.1, 10}, {0, 255}, {0, 255}}),
        // Master-gamma curves — a real curve editor is post-M4, but a
        // gamma LUT rides AdjustCurves today.
        Op("curves", "Curves (gamma)",
           {{"gamma", 1.0}},
           {{0.1, 10}}),
        Op("color_balance", "Color Balance",
           {{"shadow_r", 0.0}, {"shadow_g", 0.0}, {"shadow_b", 0.0},
            {"mid_r", 0.0}, {"mid_g", 0.0}, {"mid_b", 0.0},
            {"high_r", 0.0}, {"high_g", 0.0}, {"high_b", 0.0}},
           {{-100, 100}, {-100, 100}, {-100, 100},
            {-100, 100}, {-100, 100}, {-100, 100},
            {-100, 100}, {-100, 100}, {-100, 100}}),
        Op("vibrance", "Vibrance",
           {{"vibrance", 0.0}},
           {{-100, 100}}),
        // out_x = in_r*r + in_g*g + in_b*b + constant (identity default).
        Op("channel_mixer", "Channel Mixer",
           {{"r_r", 1.0}, {"r_g", 0.0}, {"r_b", 0.0}, {"r_c", 0.0},
            {"g_r", 0.0}, {"g_g", 1.0}, {"g_b", 0.0}, {"g_c", 0.0},
            {"b_r", 0.0}, {"b_g", 0.0}, {"b_b", 1.0}, {"b_c", 0.0}},
           {{-2, 2}, {-2, 2}, {-2, 2}, {-255, 255},
            {-2, 2}, {-2, 2}, {-2, 2}, {-255, 255},
            {-2, 2}, {-2, 2}, {-2, 2}, {-255, 255}}),
        Op("opacity", "Opacity",
           {{"opacity", 1.0}},
           {{0, 1}}),
        // range: index into the bridge's named color ranges
        // (0=reds..8=blacks); deltas are CMYK-style [-100,100].
        Op("selective_color", "Selective Color",
           {{"range", 0.0}, {"cyan", 0.0}, {"magenta", 0.0},
            {"yellow", 0.0}, {"black", 0.0}},
           {{0, 8}, {-100, 100}, {-100, 100}, {-100, 100},
            {-100, 100}}),
        Op("invert", "Invert", {}, {}),
        Op("grayscale", "Black & White", {}, {}),
        Op("sepia", "Sepia",
           {{"intensity", 1.0}},
           {{0, 1}}),
        Op("threshold", "Threshold",
           {{"value", 128.0}},
           {{0, 255}}),
        Op("posterize", "Posterize",
           {{"levels", 4.0}},
           {{2, 255}}),
    };
    return kOps;
}

// Resolves spec param `name` at schema index `i`, clamped to range.
double Param(const AdjustmentSpec& spec, const AdjustmentOpInfo& info,
             size_t i) {
    double v = info.defaults[i].value;
    const std::string& name = info.defaults[i].name;
    for (const AdjustmentParam& p : spec.params) {
        if (p.name == name) {
            v = p.value;
            break;
        }
    }
    if (i < info.ranges.size()) {
        v = std::clamp(v, info.ranges[i].first, info.ranges[i].second);
    }
    if (!std::isfinite(v)) v = info.defaults[i].value;
    return v;
}

}  // namespace

const std::vector<AdjustmentOpInfo>& AdjustmentOpRegistry() {
    return Ops();
}

const AdjustmentOpInfo* FindAdjustmentOp(const std::string& name) {
    for (const AdjustmentOpInfo& info : Ops()) {
        if (info.name == name) return &info;
    }
    return nullptr;
}

std::vector<std::string> AdjustmentOpLabels() {
    std::vector<std::string> out;
    out.reserve(Ops().size());
    for (const AdjustmentOpInfo& info : Ops()) out.push_back(info.label);
    return out;
}

AdjustmentSpec DefaultAdjustmentSpec(const std::string& op_name) {
    AdjustmentSpec spec;
    spec.op = op_name;
    if (const AdjustmentOpInfo* info = FindAdjustmentOp(op_name)) {
        spec.params = info->defaults;
    }
    return spec;
}

std::string AdjustmentParamsToText(const AdjustmentSpec& spec) {
    std::string out;
    char buf[64];
    for (size_t i = 0; i < spec.params.size(); ++i) {
        if (i > 0) out += ';';
        std::snprintf(buf, sizeof(buf), "%s=%g",
                      spec.params[i].name.c_str(),
                      spec.params[i].value);
        out += buf;
    }
    return out;
}

bool AdjustmentParamsFromText(const std::string& text,
                              AdjustmentSpec* spec) {
    if (spec == nullptr) return false;
    bool any = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t end = text.find(';', pos);
        const std::string pair =
            text.substr(pos, end == std::string::npos
                               ? std::string::npos
                               : end - pos);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos && eq > 0) {
            std::string name = pair.substr(0, eq);
            while (!name.empty() &&
                   std::isspace(
                       static_cast<unsigned char>(name.back()))) {
                name.pop_back();
            }
            size_t first = 0;
            while (first < name.size() &&
                   std::isspace(static_cast<unsigned char>(
                       name[first]))) {
                ++first;
            }
            name = name.substr(first);
            const double v =
                std::strtod(pair.c_str() + eq + 1, nullptr);
            for (AdjustmentParam& p : spec->params) {
                if (p.name == name) {
                    p.value = v;
                    any = true;
                }
            }
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return any;
}

bool ApplyAdjustment(const AdjustmentSpec& spec,
                     std::vector<uint32_t>* px, int w, int h) {
    const AdjustmentOpInfo* info = FindAdjustmentOp(spec.op);
    if (info == nullptr || px == nullptr || w <= 0 || h <= 0 ||
        px->size() < static_cast<size_t>(w) * h) {
        return false;
    }
    namespace Skia = XER::Bridge::Skia;
    const std::string& op = spec.op;
    const auto I = [&](size_t i) {
        return static_cast<int>(std::lround(Param(spec, *info, i)));
    };
    const auto D = [&](size_t i) { return Param(spec, *info, i); };

    if (op == "brightness_contrast") {
        return Skia::AdjustBrightnessContrast(px, w, h, I(0), I(1));
    }
    if (op == "hsl") {
        return Skia::AdjustHSL(px, w, h, D(0), D(1), D(2));
    }
    if (op == "exposure") {
        return Skia::AdjustExposure(px, w, h, D(0), D(1));
    }
    if (op == "levels") {
        return Skia::AdjustLevels(px, w, h, I(0), I(1), D(2), I(3),
                                  I(4));
    }
    if (op == "curves") {
        const double g = D(0);
        if (std::abs(g - 1.0) < 1e-6) return true;
        std::vector<uint8_t> lut(256);
        for (int i = 0; i < 256; ++i) {
            const double t =
                std::pow(static_cast<double>(i) / 255.0, 1.0 / g);
            lut[static_cast<size_t>(i)] = static_cast<uint8_t>(
                std::clamp(std::lround(t * 255.0), 0L, 255L));
        }
        return Skia::AdjustCurves(px, w, h, lut.data(), lut.data(),
                                  lut.data(), nullptr);
    }
    if (op == "color_balance") {
        return Skia::AdjustColorBalance(px, w, h, I(0), I(1), I(2),
                                        I(3), I(4), I(5), I(6), I(7),
                                        I(8));
    }
    if (op == "vibrance") {
        return Skia::AdjustVibrance(px, w, h, D(0));
    }
    if (op == "channel_mixer") {
        const Skia::ChannelMixerCoeff cr{D(0), D(1), D(2), D(3)};
        const Skia::ChannelMixerCoeff cg{D(4), D(5), D(6), D(7)};
        const Skia::ChannelMixerCoeff cb{D(8), D(9), D(10), D(11)};
        return Skia::AdjustChannelMixer(px, w, h, cr, cg, cb);
    }
    if (op == "opacity") {
        return Skia::AdjustOpacity(px, w, h,
                                   static_cast<float>(D(0)));
    }
    if (op == "selective_color") {
        static const char* const kRanges[] = {
            "reds",    "yellows",  "greens", "cyans", "blues",
            "magentas", "whites",  "neutrals", "blacks"};
        const int ri = std::clamp(I(0), 0, 8);
        return Skia::AdjustSelectiveColor(px, w, h, kRanges[ri],
                                          I(1), I(2), I(3), I(4));
    }
    if (op == "invert") {
        return Skia::AdjustInvert(px, w, h);
    }
    if (op == "grayscale") {
        return Skia::AdjustGrayscale(px, w, h);
    }
    if (op == "sepia") {
        return Skia::AdjustSepia(px, w, h, D(0));
    }
    if (op == "threshold") {
        return Skia::AdjustThreshold(px, w, h, I(0));
    }
    if (op == "posterize") {
        return Skia::AdjustPosterize(px, w, h, I(0));
    }
    return false;
}

}  // namespace XER::VideoEditor::Photo
