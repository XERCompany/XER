// .xerproj JSON v1 load/save — see project_store.h for the contract.
// Serialization is a straight mapping onto the spec key layout
// (version / canvas / tracks / clips / transitions); validation is
// strict for primary data and self-healing for derived data
// (transitions), matching the timeline model's invariants.
#include "project_store.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <utility>

#include <json/json.h>

#include <cstdlib>

#include "helper/path_utils.h"

namespace XER::VideoEditor {
namespace {

namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers

bool Fail(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

// ---- enum <-> spec strings (unknown strings are load errors) ---------

const char* TrackKindToString(TrackKind kind) {
    switch (kind) {
        case TrackKind::Video:
            return "video";
        case TrackKind::Audio:
            return "audio";
    }
    return "video";  // unreachable — keeps /W4 happy about no return
}

bool TrackKindFromString(const std::string& text, TrackKind* out) {
    if (text == "video") {
        *out = TrackKind::Video;
        return true;
    }
    if (text == "audio") {
        *out = TrackKind::Audio;
        return true;
    }
    return false;
}

const char* BlendModeToString(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:     return "normal";
        case BlendMode::Add:        return "add";
        case BlendMode::Multiply:   return "multiply";
        case BlendMode::Screen:     return "screen";
        case BlendMode::Overlay:    return "overlay";
        case BlendMode::Darken:     return "darken";
        case BlendMode::Lighten:    return "lighten";
        case BlendMode::Clear:      return "clear";
        case BlendMode::Src:        return "src";
        case BlendMode::Dst:        return "dst";
        case BlendMode::SrcOver:    return "src_over";
        case BlendMode::DstOver:    return "dst_over";
        case BlendMode::SrcIn:      return "src_in";
        case BlendMode::DstIn:      return "dst_in";
        case BlendMode::SrcOut:     return "src_out";
        case BlendMode::DstOut:     return "dst_out";
        case BlendMode::SrcATop:    return "src_atop";
        case BlendMode::DstATop:    return "dst_atop";
        case BlendMode::Xor:        return "xor";
        case BlendMode::Plus:       return "plus";
        case BlendMode::Modulate:   return "modulate";
        case BlendMode::ColorDodge: return "color_dodge";
        case BlendMode::ColorBurn:  return "color_burn";
        case BlendMode::HardLight:  return "hard_light";
        case BlendMode::SoftLight:  return "soft_light";
        case BlendMode::Difference: return "difference";
        case BlendMode::Exclusion:  return "exclusion";
        case BlendMode::Hue:        return "hue";
        case BlendMode::Saturation: return "saturation";
        case BlendMode::Color:      return "color";
        case BlendMode::Luminosity: return "luminosity";
    }
    return "normal";  // unreachable
}

bool BlendModeFromString(const std::string& text, BlendMode* out) {
    static const struct {
        const char* name;
        BlendMode mode;
    } kModes[] = {
        // The original seven keep their editor wire names; the extended
        // set uses snake_case spellings accepted by the Skia bridge's
        // NormalizeBlendModeName aliases.
        {"normal", BlendMode::Normal},
        {"add", BlendMode::Add},
        {"multiply", BlendMode::Multiply},
        {"screen", BlendMode::Screen},
        {"overlay", BlendMode::Overlay},
        {"darken", BlendMode::Darken},
        {"lighten", BlendMode::Lighten},
        {"clear", BlendMode::Clear},
        {"src", BlendMode::Src},
        {"dst", BlendMode::Dst},
        {"src_over", BlendMode::SrcOver},
        {"dst_over", BlendMode::DstOver},
        {"src_in", BlendMode::SrcIn},
        {"dst_in", BlendMode::DstIn},
        {"src_out", BlendMode::SrcOut},
        {"dst_out", BlendMode::DstOut},
        {"src_atop", BlendMode::SrcATop},
        {"dst_atop", BlendMode::DstATop},
        {"xor", BlendMode::Xor},
        {"plus", BlendMode::Plus},
        {"modulate", BlendMode::Modulate},
        {"color_dodge", BlendMode::ColorDodge},
        {"color_burn", BlendMode::ColorBurn},
        {"hard_light", BlendMode::HardLight},
        {"soft_light", BlendMode::SoftLight},
        {"difference", BlendMode::Difference},
        {"exclusion", BlendMode::Exclusion},
        {"hue", BlendMode::Hue},
        {"saturation", BlendMode::Saturation},
        {"color", BlendMode::Color},
        {"luminosity", BlendMode::Luminosity},
    };
    for (const auto& entry : kModes) {
        if (text == entry.name) {
            *out = entry.mode;
            return true;
        }
    }
    return false;
}

bool MaskShapeFromString(const std::string& text, MaskShape* out) {
    if (text == "rect") { *out = MaskShape::Rect; return true; }
    if (text == "ellipse") { *out = MaskShape::Ellipse; return true; }
    return false;
}

const char* MaskShapeToString(MaskShape shape) {
    return shape == MaskShape::Rect ? "rect" : "ellipse";
}

bool KeyPropFromString(const std::string& text, KeyProp* out) {
    static const struct {
        const char* name;
        KeyProp prop;
    } kProps[] = {
        {"pos_x", KeyProp::PosX},
        {"pos_y", KeyProp::PosY},
        {"scale", KeyProp::Scale},
        {"rotation", KeyProp::Rotation},
        {"opacity", KeyProp::Opacity},
        {"gain_db", KeyProp::GainDb},
    };
    for (const auto& entry : kProps) {
        if (text == entry.name) {
            *out = entry.prop;
            return true;
        }
    }
    return false;
}

const char* KeyPropToString(KeyProp prop) {
    switch (prop) {
        case KeyProp::PosX: return "pos_x";
        case KeyProp::PosY: return "pos_y";
        case KeyProp::Scale: return "scale";
        case KeyProp::Rotation: return "rotation";
        case KeyProp::Opacity: return "opacity";
        case KeyProp::GainDb: return "gain_db";
    }
    return "pos_x";  // unreachable
}

bool KeyEaseFromString(const std::string& text, KeyEase* out) {
    static const struct {
        const char* name;
        KeyEase ease;
    } kEases[] = {
        {"linear", KeyEase::Linear},
        {"ease_in", KeyEase::EaseIn},
        {"ease_out", KeyEase::EaseOut},
        {"ease_in_out", KeyEase::EaseInOut},
        {"hold", KeyEase::Hold},
        {"ease_in_cubic", KeyEase::EaseInCubic},
        {"ease_out_cubic", KeyEase::EaseOutCubic},
        {"expo_out", KeyEase::ExpoOut},
        {"back", KeyEase::Back},
        {"elastic", KeyEase::Elastic},
        {"bounce", KeyEase::Bounce},
    };
    for (const auto& entry : kEases) {
        if (text == entry.name) {
            *out = entry.ease;
            return true;
        }
    }
    return false;
}

bool SourceKindFromString(const std::string& text, SourceKind* out) {
    static const struct {
        const char* name;
        SourceKind kind;
    } kKinds[] = {
        {"media", SourceKind::Media},
        {"color_matte", SourceKind::ColorMatte},
        {"color_bars", SourceKind::ColorBars},
        {"text", SourceKind::Text},
    };
    for (const auto& entry : kKinds) {
        if (text == entry.name) {
            *out = entry.kind;
            return true;
        }
    }
    return false;
}

const char* SourceKindToString(SourceKind kind) {
    switch (kind) {
        case SourceKind::Media: return "media";
        case SourceKind::ColorMatte: return "color_matte";
        case SourceKind::ColorBars: return "color_bars";
        case SourceKind::Text: return "text";
    }
    return "media";  // unreachable
}

const char* KeyEaseToString(KeyEase ease) {
    switch (ease) {
        case KeyEase::Linear: return "linear";
        case KeyEase::EaseIn: return "ease_in";
        case KeyEase::EaseOut: return "ease_out";
        case KeyEase::EaseInOut: return "ease_in_out";
        case KeyEase::Hold: return "hold";
        case KeyEase::EaseInCubic: return "ease_in_cubic";
        case KeyEase::EaseOutCubic: return "ease_out_cubic";
        case KeyEase::ExpoOut: return "expo_out";
        case KeyEase::Back: return "back";
        case KeyEase::Elastic: return "elastic";
        case KeyEase::Bounce: return "bounce";
    }
    return "linear";  // unreachable
}

// Typed field reader bound to one JSON object. An absent key yields
// `fallback`; a present key of the wrong JSON type is a hard error —
// jsoncpp would otherwise silently convert (e.g. "5" -> 5), which is
// exactly the corruption this loader must reject.
struct Reader {
    const Json::Value& obj;
    const std::string where;
    std::string* error;

    bool TypeError(const char* key, const char* expected) const {
        return Fail(error, where + "." + key + ": expected " + expected);
    }

    bool GetF64(const char* key, double fallback, double* out) const {
        if (!obj.isMember(key)) {
            *out = fallback;
            return true;
        }
        const Json::Value& v = obj[key];
        // isDouble() accepts every JSON number type (int/uint/real).
        if (!v.isDouble()) {
            return TypeError(key, "a number");
        }
        *out = v.asDouble();
        return true;
    }

    bool GetF32(const char* key, float fallback, float* out) const {
        double d = 0.0;
        if (!GetF64(key, static_cast<double>(fallback), &d)) {
            return false;
        }
        // float-origin values round-trip exactly through double.
        *out = static_cast<float>(d);
        return true;
    }

    bool GetI64(const char* key, std::int64_t fallback,
                std::int64_t* out) const {
        if (!obj.isMember(key)) {
            *out = fallback;
            return true;
        }
        const Json::Value& v = obj[key];
        // isInt64() accepts ints and integral reals in range.
        if (!v.isInt64()) {
            return TypeError(key, "an integer");
        }
        *out = v.asInt64();
        return true;
    }

    bool GetBool(const char* key, bool fallback, bool* out) const {
        if (!obj.isMember(key)) {
            *out = fallback;
            return true;
        }
        const Json::Value& v = obj[key];
        if (!v.isBool()) {
            return TypeError(key, "a boolean");
        }
        *out = v.asBool();
        return true;
    }

    bool GetStr(const char* key, const std::string& fallback,
                std::string* out) const {
        if (!obj.isMember(key)) {
            *out = fallback;
            return true;
        }
        const Json::Value& v = obj[key];
        if (!v.isString()) {
            return TypeError(key, "a string");
        }
        *out = v.asString();
        return true;
    }
};

// ---- load -------------------------------------------------------------

bool ProjectFromJson(const Json::Value& root, Project* project_out,
                     std::string* error_out) {
    if (!root.isObject()) {
        return Fail(error_out, "project root is not a JSON object");
    }

    Project project;  // struct defaults cover every absent field

    // version gate: absent -> kVersion; anything else -> error.
    {
        Reader r{root, "project", error_out};
        std::int64_t version = ProjectStore::kVersion;
        if (!r.GetI64("version", ProjectStore::kVersion, &version)) {
            return false;
        }
        if (version < ProjectStore::kMinSupportedVersion ||
            version > ProjectStore::kVersion) {
            return Fail(
                error_out,
                "unsupported project version " + std::to_string(version) +
                    " (supported " +
                    std::to_string(ProjectStore::kMinSupportedVersion) +
                    ".." + std::to_string(ProjectStore::kVersion) + ")");
        }
    }

    // canvas: optional object; ints must be positive when present.
    if (root.isMember("canvas")) {
        const Json::Value& c = root["canvas"];
        if (!c.isObject()) {
            return Fail(error_out, "canvas: expected object");
        }
        Reader r{c, "canvas", error_out};
        std::int64_t ints[4] = {0, 0, 0, 0};
        const char* keys[4] = {"width", "height", "fps_num", "fps_den"};
        int* outs[4] = {&project.width, &project.height, &project.fps_num,
                        &project.fps_den};
        const std::int64_t fallbacks[4] = {
            project.width, project.height, project.fps_num, project.fps_den};
        for (int i = 0; i < 4; ++i) {
            if (!r.GetI64(keys[i], fallbacks[i], &ints[i])) {
                return false;
            }
            if (ints[i] < 1 ||
                ints[i] > std::numeric_limits<int>::max()) {
                return Fail(error_out,
                            std::string("canvas.") + keys[i] +
                                ": must be a positive integer");
            }
            *outs[i] = static_cast<int>(ints[i]);
        }
        if (c.isMember("bg_color")) {
            const Json::Value& bg = c["bg_color"];
            if (!bg.isArray() || bg.size() != 4u) {
                return Fail(error_out,
                            "canvas.bg_color: expected array of 4 ints");
            }
            for (Json::ArrayIndex i = 0; i < bg.size(); ++i) {
                const Json::Value& e = bg[i];
                if (!e.isInt64()) {
                    return Fail(error_out,
                                "canvas.bg_color[" + std::to_string(i) +
                                    "]: expected int");
                }
                const std::int64_t x = e.asInt64();
                if (x < 0 || x > 255) {
                    return Fail(error_out,
                                "canvas.bg_color[" + std::to_string(i) +
                                    "]: out of range 0-255");
                }
                project.bg_color[i] = static_cast<std::uint8_t>(x);
            }
        }
    }

    // tracks: optional array; kind defaults to Video when absent.
    if (root.isMember("tracks")) {
        const Json::Value& tv = root["tracks"];
        if (!tv.isArray()) {
            return Fail(error_out, "tracks: expected array");
        }
        for (Json::ArrayIndex i = 0; i < tv.size(); ++i) {
            const Json::Value& tj = tv[i];
            const std::string where = "track[" + std::to_string(i) + "]";
            if (!tj.isObject()) {
                return Fail(error_out, where + ": expected object");
            }
            Reader r{tj, where, error_out};
            Track track;
            if (tj.isMember("kind")) {
                std::string kind_text;
                if (!r.GetStr("kind", "", &kind_text)) {
                    return false;
                }
                if (!TrackKindFromString(kind_text, &track.kind)) {
                    return Fail(error_out, where + ".kind: unknown kind '" +
                                             kind_text + "'");
                }
            }
            if (!r.GetStr("name", "", &track.name) ||
                !r.GetBool("muted", track.muted, &track.muted) ||
                !r.GetBool("hidden", track.hidden, &track.hidden) ||
                !r.GetBool("locked", track.locked, &track.locked) ||
                !r.GetF32("gain_db", track.gain_db, &track.gain_db) ||
                !r.GetF32("pan", track.pan, &track.pan)) {
                return false;
            }
            if (!std::isfinite(track.pan) || track.pan < -1.0f ||
                track.pan > 1.0f) {
                return Fail(error_out,
                            where + ".pan: out of range [-1, 1]");
            }
            project.tracks.push_back(track);
        }
    }

    // Per-clip parse — shared by the top-level "clips" array and each
    // stored sequence's inner clips (sequence-local tracks are passed
    // the same way). `existing` is the clip list parsed so far, used
    // for the unique-id check.
    const auto parse_clip = [&](const Json::Value& cj,
                                const std::string& where,
                                const std::vector<Track>& tracks,
                                const std::vector<Clip>& existing,
                                Clip* clip_out) -> bool {
            Reader r{cj, where, error_out};
            Clip clip;

            if (!r.GetStr("id", "", &clip.id)) {
                return false;
            }
            if (clip.id.empty()) {
                return Fail(error_out, where + ".id: required non-empty");
            }
            const bool duplicate =
                std::any_of(existing.begin(), existing.end(),
                            [&](const Clip& c) { return c.id == clip.id; });
            if (duplicate) {
                return Fail(error_out,
                            where + ".id: duplicate clip id '" + clip.id +
                                "'");
            }
            if (!r.GetStr("source", "", &clip.source_path)) {
                return false;
            }
            // Optional user label (absent on load -> "").
            if (!r.GetStr("name", clip.name, &clip.name)) {
                return false;
            }

            std::int64_t track_index = 0;
            if (!r.GetI64("track", 0, &track_index)) {
                return false;
            }
            if (track_index < 0 ||
                track_index >=
                    static_cast<std::int64_t>(tracks.size())) {
                return Fail(error_out,
                            where + ".track: index out of range");
            }
            clip.track = static_cast<int>(track_index);

            double t = 0.0;
            if (!r.GetF64("timeline_start", 0.0, &t)) {
                return false;
            }
            if (!std::isfinite(t) || t < 0.0) {
                return Fail(error_out,
                            where + ".timeline_start: must be >= 0");
            }
            clip.timeline_start = t;
            if (!r.GetF64("in_point", 0.0, &t)) {
                return false;
            }
            if (!std::isfinite(t) || t < 0.0) {
                return Fail(error_out, where + ".in_point: must be >= 0");
            }
            clip.in_point = t;
            if (!r.GetF64("duration", 0.0, &t)) {
                return false;
            }
            if (!std::isfinite(t) || t <= 0.0) {
                return Fail(error_out, where + ".duration: must be > 0");
            }
            clip.duration = t;

            if (!r.GetBool("muted", clip.muted, &clip.muted) ||
                !r.GetBool("adjustment", clip.is_adjustment,
                           &clip.is_adjustment) ||
                !r.GetF32("gain_db", clip.gain_db, &clip.gain_db) ||
                !r.GetF64("fade_in", clip.fade_in, &clip.fade_in) ||
                !r.GetF64("fade_out", clip.fade_out, &clip.fade_out) ||
                !r.GetF64("playback_rate", clip.playback_rate,
                          &clip.playback_rate) ||
                !r.GetF32("pan", clip.pan, &clip.pan) ||
                !r.GetStr("link_group", clip.link_group,
                          &clip.link_group)) {
                return false;
            }
            if (!std::isfinite(clip.playback_rate) ||
                clip.playback_rate <= 0.0) {
                return Fail(error_out,
                            where + ".playback_rate: must be > 0");
            }
            if (!std::isfinite(clip.pan) || clip.pan < -1.0f ||
                clip.pan > 1.0f) {
                return Fail(error_out,
                            where + ".pan: out of range [-1, 1]");
            }
            if (cj.isMember("blend_mode")) {
                std::string mode_text;
                if (!r.GetStr("blend_mode", "", &mode_text)) {
                    return false;
                }
                if (!BlendModeFromString(mode_text, &clip.blend_mode)) {
                    return Fail(error_out,
                                where + ".blend_mode: unknown mode '" +
                                    mode_text + "'");
                }
            }
            if (cj.isMember("source_kind")) {
                std::string kind_text;
                if (!r.GetStr("source_kind", "", &kind_text)) {
                    return false;
                }
                if (!SourceKindFromString(kind_text,
                                          &clip.source.kind)) {
                    return Fail(error_out,
                                where + ".source_kind: unknown kind '" +
                                    kind_text + "'");
                }
            }
            if (!r.GetStr("source_param", clip.source.param,
                          &clip.source.param)) {
                return false;
            }
            if (clip.source.kind == SourceKind::Media &&
                !clip.is_adjustment && clip.compound.empty() &&
                clip.source_path.empty()) {
                return Fail(error_out,
                            where + ".source: required non-empty");
            }
            // Compound clip: "compound" stores the referenced sequence
            // id (v3+). Resolution + cycle checks run after all
            // sequences are parsed.
            if (!r.GetStr("compound", clip.compound, &clip.compound)) {
                return false;
            }

            // transform: optional object; absent fields keep Transform{}.
            if (cj.isMember("transform")) {
                const Json::Value& tj = cj["transform"];
                const std::string twhere = where + ".transform";
                if (!tj.isObject()) {
                    return Fail(error_out, twhere + ": expected object");
                }
                Reader tr{tj, twhere, error_out};
                Transform transform;
                if (!tr.GetF32("offset_x", transform.offset_x,
                               &transform.offset_x) ||
                    !tr.GetF32("offset_y", transform.offset_y,
                               &transform.offset_y) ||
                    !tr.GetF32("scale", transform.scale,
                               &transform.scale) ||
                    !tr.GetF32("rotation_deg", transform.rotation_deg,
                               &transform.rotation_deg) ||
                    !tr.GetF32("opacity", transform.opacity,
                               &transform.opacity)) {
                    return false;
                }
                clip.transform = transform;
            }

            // effects: optional array of {type, params[]}.
            if (cj.isMember("effects")) {
                const Json::Value& ej = cj["effects"];
                if (!ej.isArray()) {
                    return Fail(error_out,
                                where + ".effects: expected array");
                }
                for (Json::ArrayIndex j = 0; j < ej.size(); ++j) {
                    const Json::Value& ee = ej[j];
                    const std::string ewhere =
                        where + ".effects[" + std::to_string(j) + "]";
                    if (!ee.isObject()) {
                        return Fail(error_out, ewhere + ": expected object");
                    }
                    Reader er{ee, ewhere, error_out};
                    ClipEffect effect;
                    if (!er.GetStr("type", "", &effect.type)) {
                        return false;
                    }
                    if (ee.isMember("params")) {
                        const Json::Value& pj = ee["params"];
                        if (!pj.isArray()) {
                            return Fail(error_out,
                                        ewhere + ".params: expected array");
                        }
                        for (Json::ArrayIndex k = 0; k < pj.size(); ++k) {
                            const Json::Value& pv = pj[k];
                            if (!pv.isDouble()) {
                                return Fail(
                                    error_out,
                                    ewhere + ".params[" +
                                        std::to_string(k) +
                                        "]: expected number");
                            }
                            effect.params.push_back(
                                static_cast<float>(pv.asDouble()));
                        }
                    }
                    clip.effects.push_back(effect);
                }
            }

            // masks: optional array of {shape, cx, cy, w, h, feather,
            // inverted}; unknown shape strings are errors.
            if (cj.isMember("masks")) {
                const Json::Value& mj = cj["masks"];
                if (!mj.isArray()) {
                    return Fail(error_out,
                                where + ".masks: expected array");
                }
                for (Json::ArrayIndex j = 0; j < mj.size(); ++j) {
                    const Json::Value& mm = mj[j];
                    const std::string mwhere =
                        where + ".masks[" + std::to_string(j) + "]";
                    if (!mm.isObject()) {
                        return Fail(error_out,
                                    mwhere + ": expected object");
                    }
                    Reader mr{mm, mwhere, error_out};
                    ClipMask mask;
                    if (mm.isMember("shape")) {
                        std::string shape_text;
                        if (!mr.GetStr("shape", "", &shape_text)) {
                            return false;
                        }
                        if (!MaskShapeFromString(shape_text,
                                                 &mask.shape)) {
                            return Fail(
                                error_out,
                                mwhere + ".shape: unknown shape '" +
                                    shape_text + "'");
                        }
                    }
                    if (!mr.GetF32("cx", mask.cx, &mask.cx) ||
                        !mr.GetF32("cy", mask.cy, &mask.cy) ||
                        !mr.GetF32("w", mask.w, &mask.w) ||
                        !mr.GetF32("h", mask.h, &mask.h) ||
                        !mr.GetF32("feather", mask.feather,
                                   &mask.feather) ||
                        !mr.GetBool("inverted", mask.inverted,
                                    &mask.inverted)) {
                        return false;
                    }
                    clip.masks.push_back(mask);
                }
            }

            // keyframes: optional object {prop_name: [{time, value,
            // ease}]}; unknown property/ease strings are errors.
            if (cj.isMember("keyframes")) {
                const Json::Value& kj = cj["keyframes"];
                if (!kj.isObject()) {
                    return Fail(error_out,
                                where + ".keyframes: expected object");
                }
                for (const std::string& prop_name : kj.getMemberNames()) {
                    KeyProp prop;
                    if (!KeyPropFromString(prop_name, &prop)) {
                        return Fail(error_out,
                                    where + ".keyframes: unknown "
                                            "property '" +
                                        prop_name + "'");
                    }
                    const Json::Value& kv = kj[prop_name];
                    if (!kv.isArray()) {
                        return Fail(error_out,
                                    where + ".keyframes." + prop_name +
                                        ": expected array");
                    }
                    std::vector<Keyframe> keys;
                    for (Json::ArrayIndex j = 0; j < kv.size(); ++j) {
                        const Json::Value& kk = kv[j];
                        const std::string kwhere =
                            where + ".keyframes." + prop_name + "[" +
                            std::to_string(j) + "]";
                        if (!kk.isObject()) {
                            return Fail(error_out,
                                        kwhere + ": expected object");
                        }
                        Reader kr{kk, kwhere, error_out};
                        Keyframe key;
                        if (!kr.GetF64("time", key.time, &key.time) ||
                            !kr.GetF32("value", key.value, &key.value)) {
                            return false;
                        }
                        if (kk.isMember("ease")) {
                            std::string ease_text;
                            if (!kr.GetStr("ease", "", &ease_text)) {
                                return false;
                            }
                            if (!KeyEaseFromString(ease_text,
                                                   &key.easing)) {
                                return Fail(
                                    error_out,
                                    kwhere + ".ease: unknown ease '" +
                                        ease_text + "'");
                            }
                        }
                        keys.push_back(key);
                    }
                    clip.keyframes[prop] = std::move(keys);
                }
            }

            // audio_fx: optional array of hosted plugins
            // {path, name, enabled, params: {"<id>": value}}.
            if (cj.isMember("audio_fx")) {
                const Json::Value& fj = cj["audio_fx"];
                if (!fj.isArray()) {
                    return Fail(error_out,
                                where + ".audio_fx: expected array");
                }
                for (Json::ArrayIndex j = 0; j < fj.size(); ++j) {
                    const Json::Value& ff = fj[j];
                    const std::string fwhere =
                        where + ".audio_fx[" + std::to_string(j) + "]";
                    if (!ff.isObject()) {
                        return Fail(error_out,
                                    fwhere + ": expected object");
                    }
                    Reader fr{ff, fwhere, error_out};
                    ClipAudioFx fx;
                    if (!fr.GetStr("path", "", &fx.plugin_path)) {
                        return false;
                    }
                    if (fx.plugin_path.empty()) {
                        return Fail(error_out,
                                    fwhere + ".path: required non-empty");
                    }
                    if (!fr.GetStr("name", fx.name, &fx.name) ||
                        !fr.GetBool("enabled", fx.enabled,
                                    &fx.enabled)) {
                        return false;
                    }
                    if (ff.isMember("params")) {
                        const Json::Value& pj = ff["params"];
                        if (!pj.isObject()) {
                            return Fail(
                                error_out,
                                fwhere + ".params: expected object");
                        }
                        for (const std::string& pid :
                             pj.getMemberNames()) {
                            const Json::Value& pv = pj[pid];
                            if (!pv.isDouble()) {
                                return Fail(error_out,
                                            fwhere + ".params." + pid +
                                                ": expected number");
                            }
                            char* end = nullptr;
                            const unsigned long id =
                                std::strtoul(pid.c_str(), &end, 10);
                            if (end == pid.c_str() || *end != '\0') {
                                return Fail(error_out,
                                            fwhere + ".params: bad id '" +
                                                pid + "'");
                            }
                            fx.params[static_cast<std::uint32_t>(id)] =
                                static_cast<float>(pv.asDouble());
                        }
                    }
                    clip.audio_fx.push_back(std::move(fx));
                }
            }

            // tags: optional array of AI find-objects ranges
            // {start, end, label, confidence} in SOURCE seconds
            // (ClipTag contract — timeline_model.h). Wrong types are
            // hard errors like the rest of primary clip data; a
            // reversed range self-heals to a point tag, non-finite
            // numbers drop the entry.
            if (cj.isMember("tags")) {
                const Json::Value& tj = cj["tags"];
                if (!tj.isArray()) {
                    return Fail(error_out,
                                where + ".tags: expected array");
                }
                for (Json::ArrayIndex j = 0; j < tj.size(); ++j) {
                    const Json::Value& tt = tj[j];
                    const std::string twhere =
                        where + ".tags[" + std::to_string(j) + "]";
                    if (!tt.isObject()) {
                        return Fail(error_out,
                                    twhere + ": expected object");
                    }
                    Reader tr{tt, twhere, error_out};
                    ClipTag tag;
                    if (!tr.GetF64("start", tag.start, &tag.start) ||
                        !tr.GetF64("end", tag.end, &tag.end) ||
                        !tr.GetStr("label", tag.label, &tag.label) ||
                        !tr.GetF32("confidence", tag.confidence,
                                   &tag.confidence)) {
                        return false;
                    }
                    if (!std::isfinite(tag.start) ||
                        !std::isfinite(tag.end) ||
                        !std::isfinite(tag.confidence)) {
                        continue;   // corrupt entry — drop, keep loading
                    }
                    if (tag.end < tag.start) tag.end = tag.start;
                    clip.tags.push_back(std::move(tag));
                }
            }

            // reframe: optional AI smart-reframe subject track —
            // {samples: [{t, cx, cy, w, h, conf, cut}]} in SOURCE
            // seconds / frame fractions (ReframeSample contract).
            // Same policy as tags: wrong types are hard errors,
            // non-finite numbers drop the entry.
            if (cj.isMember("reframe")) {
                const Json::Value& rj = cj["reframe"];
                if (!rj.isObject()) {
                    return Fail(error_out,
                                where + ".reframe: expected object");
                }
                if (rj.isMember("samples")) {
                    const Json::Value& sj = rj["samples"];
                    if (!sj.isArray()) {
                        return Fail(
                            error_out,
                            where + ".reframe.samples: expected array");
                    }
                    for (Json::ArrayIndex j = 0; j < sj.size(); ++j) {
                        const Json::Value& ss = sj[j];
                        const std::string swhere =
                            where + ".reframe.samples[" +
                            std::to_string(j) + "]";
                        if (!ss.isObject()) {
                            return Fail(error_out,
                                        swhere + ": expected object");
                        }
                        Reader sr{ss, swhere, error_out};
                        ReframeSample sample;
                        if (!sr.GetF64("t", sample.t, &sample.t) ||
                            !sr.GetF32("cx", sample.cx, &sample.cx) ||
                            !sr.GetF32("cy", sample.cy, &sample.cy) ||
                            !sr.GetF32("w", sample.w, &sample.w) ||
                            !sr.GetF32("h", sample.h, &sample.h) ||
                            !sr.GetF32("conf", sample.confidence,
                                       &sample.confidence) ||
                            !sr.GetBool("cut", sample.scene_boundary,
                                        &sample.scene_boundary)) {
                            return false;
                        }
                        if (!std::isfinite(sample.t) ||
                            !std::isfinite(sample.cx) ||
                            !std::isfinite(sample.cy) ||
                            !std::isfinite(sample.w) ||
                            !std::isfinite(sample.h) ||
                            !std::isfinite(sample.confidence)) {
                            continue;   // corrupt entry — drop it
                        }
                        clip.reframe.samples.push_back(sample);
                    }
                }
            }

            // object_track: optional AI track-object result —
            // {label, classes: [...], samples: [{t, cx, cy, w, h,
            // conf, cut}]} in SOURCE seconds / frame fractions (the
            // ClipTrack contract — same sample shape as reframe).
            // Same policy as reframe: wrong types are hard errors,
            // non-finite numbers drop the entry.
            if (cj.isMember("object_track")) {
                const Json::Value& tj = cj["object_track"];
                const std::string twhere = where + ".object_track";
                if (!tj.isObject()) {
                    return Fail(error_out,
                                twhere + ": expected object");
                }
                Reader tr{tj, twhere, error_out};
                if (!tr.GetStr("label", clip.object_track.label,
                               &clip.object_track.label)) {
                    return false;
                }
                if (tj.isMember("classes")) {
                    const Json::Value& cl = tj["classes"];
                    if (!cl.isArray()) {
                        return Fail(
                            error_out,
                            twhere + ".classes: expected array");
                    }
                    for (Json::ArrayIndex j = 0; j < cl.size(); ++j) {
                        if (!cl[j].isString()) {
                            return Fail(
                                error_out,
                                twhere + ".classes[" +
                                    std::to_string(j) +
                                    "]: expected string");
                        }
                        clip.object_track.classes.push_back(
                            cl[j].asString());
                    }
                }
                if (tj.isMember("samples")) {
                    const Json::Value& sj = tj["samples"];
                    if (!sj.isArray()) {
                        return Fail(
                            error_out,
                            twhere + ".samples: expected array");
                    }
                    for (Json::ArrayIndex j = 0; j < sj.size(); ++j) {
                        const Json::Value& ss = sj[j];
                        const std::string swhere =
                            twhere + ".samples[" +
                            std::to_string(j) + "]";
                        if (!ss.isObject()) {
                            return Fail(error_out,
                                        swhere + ": expected object");
                        }
                        Reader sr{ss, swhere, error_out};
                        ReframeSample sample;
                        if (!sr.GetF64("t", sample.t, &sample.t) ||
                            !sr.GetF32("cx", sample.cx, &sample.cx) ||
                            !sr.GetF32("cy", sample.cy, &sample.cy) ||
                            !sr.GetF32("w", sample.w, &sample.w) ||
                            !sr.GetF32("h", sample.h, &sample.h) ||
                            !sr.GetF32("conf", sample.confidence,
                                       &sample.confidence) ||
                            !sr.GetBool("cut", sample.scene_boundary,
                                        &sample.scene_boundary)) {
                            return false;
                        }
                        if (!std::isfinite(sample.t) ||
                            !std::isfinite(sample.cx) ||
                            !std::isfinite(sample.cy) ||
                            !std::isfinite(sample.w) ||
                            !std::isfinite(sample.h) ||
                            !std::isfinite(sample.confidence)) {
                            continue;   // corrupt entry — drop it
                        }
                        clip.object_track.samples.push_back(sample);
                    }
                }
            }

            *clip_out = std::move(clip);
            return true;
    };

    // clips: required id (non-empty, unique) + source (non-empty);
    // track must index the loaded tracks; times must be >= 0 and
    // duration > 0.
    if (root.isMember("clips")) {
        const Json::Value& cv = root["clips"];
        if (!cv.isArray()) {
            return Fail(error_out, "clips: expected array");
        }
        for (Json::ArrayIndex i = 0; i < cv.size(); ++i) {
            const Json::Value& cj = cv[i];
            const std::string where = "clip[" + std::to_string(i) + "]";
            if (!cj.isObject()) {
                return Fail(error_out, where + ": expected object");
            }
            Clip clip;
            if (!parse_clip(cj, where, project.tracks, project.clips,
                            &clip)) {
                return false;
            }
            project.clips.push_back(std::move(clip));
        }
    }

    // Corrupt-primary-data check: the model's core invariant is that
    // no two clips overlap on the same track — ValidateClipPlacement
    // enforces it on every mutation, so a hand-edited file must not
    // load into a state the mutators would reject. Adjacency
    // (end == start) is allowed; this is a hard error like duplicate
    // ids, NOT self-heal.
    for (std::size_t i = 0; i < project.clips.size(); ++i) {
        for (std::size_t j = i + 1; j < project.clips.size(); ++j) {
            const Clip& a = project.clips[i];
            const Clip& b = project.clips[j];
            if (a.track == b.track && ClipsOverlap(a, b)) {
                return Fail(error_out,
                            "clip '" + a.id + "' overlaps clip '" + b.id +
                                "' on track " + std::to_string(a.track));
            }
        }
    }

    // transitions: lenient — missing endpoints and non-positive
    // durations drop (self-heal below / at parse); unknown type
    // strings are errors.
    if (root.isMember("transitions")) {
        const Json::Value& tv = root["transitions"];
        if (!tv.isArray()) {
            return Fail(error_out, "transitions: expected array");
        }
        for (Json::ArrayIndex i = 0; i < tv.size(); ++i) {
            const Json::Value& tj = tv[i];
            const std::string where = "transition[" + std::to_string(i) + "]";
            if (!tj.isObject()) {
                return Fail(error_out, where + ": expected object");
            }
            Reader r{tj, where, error_out};
            Transition transition;
            if (!r.GetStr("from", "", &transition.from_clip) ||
                !r.GetStr("to", "", &transition.to_clip)) {
                return false;
            }
            if (tj.isMember("type")) {
                std::string type_text;
                if (!r.GetStr("type", "", &type_text)) {
                    return false;
                }
                if (!TransitionTypeFromString(type_text,
                                              &transition.type)) {
                    return Fail(error_out, where + ".type: unknown type '" +
                                             type_text + "'");
                }
            }
            if (!r.GetF64("duration", transition.duration,
                         &transition.duration)) {
                return false;
            }
            // Script id for Type::Script — optional, lenient like the
            // endpoints; a missing/unknown id falls back to a linear
            // crossfade at render time.
            r.GetStr("script", "", &transition.script);
            // Non-positive/non-finite duration → invalid transition:
            // drop it (self-heal, same class as missing endpoints —
            // AddTransition requires duration > 0). Unknown `type`
            // strings above remain hard errors.
            if (!std::isfinite(transition.duration) ||
                transition.duration <= 0.0) {
                continue;
            }
            project.transitions.push_back(transition);
        }
    }

    // markers: lenient — entries with non-finite/negative times are
    // dropped; malformed entries are errors like other primary data.
    if (root.isMember("markers")) {
        const Json::Value& mv = root["markers"];
        if (!mv.isArray()) {
            return Fail(error_out, "markers: expected array");
        }
        for (Json::ArrayIndex i = 0; i < mv.size(); ++i) {
            const Json::Value& mj = mv[i];
            const std::string where =
                "marker[" + std::to_string(i) + "]";
            if (!mj.isObject()) {
                return Fail(error_out, where + ": expected object");
            }
            Reader r{mj, where, error_out};
            Marker marker;
            if (!r.GetF64("time", marker.time, &marker.time) ||
                !r.GetStr("name", marker.name, &marker.name)) {
                return false;
            }
            if (!std::isfinite(marker.time) || marker.time < 0.0) {
                continue;  // self-heal
            }
            if (mj.isMember("color")) {
                const Json::Value& cj = mj["color"];
                if (!cj.isArray() || cj.size() != 4u) {
                    return Fail(error_out,
                                where + ".color: expected array of 4");
                }
                for (Json::ArrayIndex k = 0; k < 4; ++k) {
                    const Json::Value& e = cj[k];
                    if (!e.isInt64() || e.asInt64() < 0 ||
                        e.asInt64() > 255) {
                        return Fail(error_out,
                                    where + ".color: components 0-255");
                    }
                    marker.color[k] =
                        static_cast<std::uint8_t>(e.asInt64());
                }
            }
            project.markers.push_back(marker);
        }
        std::stable_sort(project.markers.begin(), project.markers.end(),
                         [](const Marker& a, const Marker& b) {
                             return a.time < b.time;
                         });
        // The model keys markers on time (AddMarker merges equal-time
        // entries; RemoveMarker hits the first match) — a hand-edited
        // file with same-time duplicates would leave remove/rename
        // addressing the wrong marker. Dedupe keeping the LAST entry
        // of each equal-time run, matching AddMarker's later-wins
        // merge. Epsilon mirrors timeline_model's kEpsilon.
        constexpr double kMarkerTimeEps = 1e-6;
        auto kept = project.markers.begin();
        for (auto it = project.markers.begin();
             it != project.markers.end(); ++it) {
            const bool last_of_run =
                std::next(it) == project.markers.end() ||
                std::fabs(std::next(it)->time - it->time) >
                    kMarkerTimeEps;
            if (last_of_run) *kept++ = *it;
        }
        project.markers.erase(kept, project.markers.end());
    }

    {
        Reader r{root, "project", error_out};
        std::int64_t proxy_max_dim = project.proxy_max_dim;
        std::int64_t proxy_crf = project.proxy_crf;
        std::int64_t proxy_cache_max_mb = project.proxy_cache_max_mb;
        if (!r.GetF64("work_in", project.work_in, &project.work_in) ||
            !r.GetF64("work_out", project.work_out, &project.work_out) ||
            !r.GetF32("master_gain_db", project.master_gain_db,
                      &project.master_gain_db) ||
            !r.GetBool("use_proxies", project.use_proxies,
                       &project.use_proxies) ||
            !r.GetBool("proxy_ingest", project.proxy_ingest,
                       &project.proxy_ingest) ||
            !r.GetI64("proxy_max_dim", project.proxy_max_dim,
                      &proxy_max_dim) ||
            !r.GetI64("proxy_crf", project.proxy_crf, &proxy_crf) ||
            !r.GetI64("proxy_cache_max_mb", project.proxy_cache_max_mb,
                      &proxy_cache_max_mb)) {
            return false;
        }
        project.proxy_max_dim =
            static_cast<int>(std::clamp<std::int64_t>(
                proxy_max_dim, 0, 16384));
        project.proxy_crf = static_cast<int>(
            std::clamp<std::int64_t>(proxy_crf, -1, 63));
        // 1 TiB of proxies is absurd — bound the field to a sane
        // ceiling (0 keeps the documented "unbounded" meaning).
        project.proxy_cache_max_mb = static_cast<int>(
            std::clamp<std::int64_t>(proxy_cache_max_mb, 0, 1048576));
        if (!std::isfinite(project.master_gain_db) ||
            project.master_gain_db < -96.0f ||
            project.master_gain_db > 24.0f) {
            return Fail(error_out,
                        "master_gain_db out of range [-96, 24]");
        }
        if (!std::isfinite(project.work_in) ||
            !std::isfinite(project.work_out) || project.work_in < 0.0 ||
            project.work_out < 0.0) {
            return Fail(error_out, "work area must be finite and >= 0");
        }
        if (project.work_out <= project.work_in) {
            project.work_in = 0.0;
            project.work_out = 0.0;
        }
    }

    // Self-heal: transitions referencing missing clips, clips on
    // different or non-video tracks, or non-adjacent clips are dropped
    // rather than failing the load.
    DropInvalidTransitions(project);
    *project_out = std::move(project);
    return true;
}

// ---- save ---------------------------------------------------------------

Json::Value TrackToJson(const Track& track) {
    Json::Value out(Json::objectValue);
    out["kind"] = TrackKindToString(track.kind);
    out["name"] = track.name;
    out["muted"] = track.muted;
    out["hidden"] = track.hidden;
    out["locked"] = track.locked;
    out["gain_db"] = static_cast<double>(track.gain_db);
    out["pan"] = static_cast<double>(track.pan);
    return out;
}

Json::Value ClipToJson(const Clip& clip) {
    Json::Value out(Json::objectValue);
    out["id"] = clip.id;
    out["source"] = clip.source_path;
    if (!clip.name.empty()) {
        out["name"] = clip.name;
    }
    out["track"] = clip.track;
    out["timeline_start"] = clip.timeline_start;
    out["in_point"] = clip.in_point;
    out["duration"] = clip.duration;
    out["muted"] = clip.muted;
    out["gain_db"] = static_cast<double>(clip.gain_db);
    out["fade_in"] = clip.fade_in;
    out["fade_out"] = clip.fade_out;
    out["playback_rate"] = clip.playback_rate;
    out["pan"] = static_cast<double>(clip.pan);
    if (clip.blend_mode != BlendMode::Normal) {
        out["blend_mode"] = BlendModeToString(clip.blend_mode);
    }
    if (clip.source.kind != SourceKind::Media) {
        out["source_kind"] = SourceKindToString(clip.source.kind);
    }
    if (!clip.source.param.empty()) {
        out["source_param"] = clip.source.param;
    }
    if (clip.is_adjustment) {
        out["adjustment"] = true;   // absent on load => false
    }
    if (!clip.link_group.empty()) {
        out["link_group"] = clip.link_group;
    }

    if (!clip.audio_fx.empty()) {
        Json::Value audio_fx(Json::arrayValue);
        for (const ClipAudioFx& fx : clip.audio_fx) {
            Json::Value fj(Json::objectValue);
            fj["path"] = fx.plugin_path;
            fj["name"] = fx.name;
            fj["enabled"] = fx.enabled;
            if (!fx.params.empty()) {
                Json::Value params(Json::objectValue);
                for (const auto& entry : fx.params) {
                    params[std::to_string(entry.first)] =
                        static_cast<double>(entry.second);
                }
                fj["params"] = params;
            }
            audio_fx.append(fj);
        }
        out["audio_fx"] = audio_fx;
    }

    if (!clip.tags.empty()) {
        Json::Value tags(Json::arrayValue);
        for (const ClipTag& tag : clip.tags) {
            Json::Value tj(Json::objectValue);
            tj["start"] = tag.start;
            tj["end"] = tag.end;
            tj["label"] = tag.label;
            tj["confidence"] = static_cast<double>(tag.confidence);
            tags.append(tj);
        }
        out["tags"] = tags;
    }

    if (!clip.reframe.samples.empty()) {
        Json::Value reframe(Json::objectValue);
        Json::Value samples(Json::arrayValue);
        for (const ReframeSample& sample : clip.reframe.samples) {
            Json::Value sj(Json::objectValue);
            sj["t"] = sample.t;
            sj["cx"] = static_cast<double>(sample.cx);
            sj["cy"] = static_cast<double>(sample.cy);
            sj["w"] = static_cast<double>(sample.w);
            sj["h"] = static_cast<double>(sample.h);
            sj["conf"] = static_cast<double>(sample.confidence);
            sj["cut"] = sample.scene_boundary;
            samples.append(sj);
        }
        reframe["samples"] = samples;
        out["reframe"] = reframe;
    }

    if (!clip.object_track.samples.empty() ||
        !clip.object_track.label.empty() ||
        !clip.object_track.classes.empty()) {
        Json::Value track(Json::objectValue);
        if (!clip.object_track.label.empty()) {
            track["label"] = clip.object_track.label;
        }
        if (!clip.object_track.classes.empty()) {
            Json::Value classes(Json::arrayValue);
            for (const std::string& label :
                 clip.object_track.classes) {
                classes.append(label);
            }
            track["classes"] = classes;
        }
        Json::Value samples(Json::arrayValue);
        for (const ReframeSample& sample :
             clip.object_track.samples) {
            Json::Value sj(Json::objectValue);
            sj["t"] = sample.t;
            sj["cx"] = static_cast<double>(sample.cx);
            sj["cy"] = static_cast<double>(sample.cy);
            sj["w"] = static_cast<double>(sample.w);
            sj["h"] = static_cast<double>(sample.h);
            sj["conf"] = static_cast<double>(sample.confidence);
            sj["cut"] = sample.scene_boundary;
            samples.append(sj);
        }
        track["samples"] = samples;
        out["object_track"] = track;
    }

    if (!clip.masks.empty()) {
        Json::Value masks(Json::arrayValue);
        for (const ClipMask& mask : clip.masks) {
            Json::Value mj(Json::objectValue);
            mj["shape"] = MaskShapeToString(mask.shape);
            mj["cx"] = static_cast<double>(mask.cx);
            mj["cy"] = static_cast<double>(mask.cy);
            mj["w"] = static_cast<double>(mask.w);
            mj["h"] = static_cast<double>(mask.h);
            mj["feather"] = static_cast<double>(mask.feather);
            mj["inverted"] = mask.inverted;
            masks.append(mj);
        }
        out["masks"] = masks;
    }

    if (!clip.keyframes.empty()) {
        Json::Value keyframes(Json::objectValue);
        for (const auto& entry : clip.keyframes) {
            Json::Value keys(Json::arrayValue);
            for (const Keyframe& key : entry.second) {
                Json::Value kj(Json::objectValue);
                kj["time"] = key.time;
                kj["value"] = static_cast<double>(key.value);
                if (key.easing != KeyEase::Linear) {
                    kj["ease"] = KeyEaseToString(key.easing);
                }
                keys.append(kj);
            }
            keyframes[KeyPropToString(entry.first)] = keys;
        }
        out["keyframes"] = keyframes;
    }

    Json::Value transform(Json::objectValue);
    transform["offset_x"] =
        static_cast<double>(clip.transform.offset_x);
    transform["offset_y"] =
        static_cast<double>(clip.transform.offset_y);
    transform["scale"] = static_cast<double>(clip.transform.scale);
    transform["rotation_deg"] =
        static_cast<double>(clip.transform.rotation_deg);
    transform["opacity"] = static_cast<double>(clip.transform.opacity);
    out["transform"] = transform;

    Json::Value effects(Json::arrayValue);
    for (const ClipEffect& effect : clip.effects) {
        Json::Value ej(Json::objectValue);
        ej["type"] = effect.type;
        Json::Value params(Json::arrayValue);
        for (const float param : effect.params) {
            params.append(static_cast<double>(param));
        }
        ej["params"] = params;
        effects.append(ej);
    }
    out["effects"] = effects;
    return out;
}

Json::Value TransitionToJson(const Transition& transition) {
    Json::Value out(Json::objectValue);
    out["from"] = transition.from_clip;
    out["to"] = transition.to_clip;
    out["type"] = TransitionTypeToString(transition.type);
    out["duration"] = transition.duration;
    if (transition.type == Transition::Type::Script) {
        out["script"] = transition.script;
    }
    return out;
}

Json::Value ProjectToJson(const Project& project) {
    Json::Value root(Json::objectValue);
    root["version"] = ProjectStore::kVersion;

    Json::Value canvas(Json::objectValue);
    canvas["width"] = project.width;
    canvas["height"] = project.height;
    canvas["fps_num"] = project.fps_num;
    canvas["fps_den"] = project.fps_den;
    Json::Value bg(Json::arrayValue);
    for (const std::uint8_t component : project.bg_color) {
        bg.append(static_cast<int>(component));
    }
    canvas["bg_color"] = bg;
    root["canvas"] = canvas;

    Json::Value tracks(Json::arrayValue);
    for (const Track& track : project.tracks) {
        tracks.append(TrackToJson(track));
    }
    root["tracks"] = tracks;

    Json::Value clips(Json::arrayValue);
    for (const Clip& clip : project.clips) {
        clips.append(ClipToJson(clip));
    }
    root["clips"] = clips;

    Json::Value transitions(Json::arrayValue);
    for (const Transition& transition : project.transitions) {
        transitions.append(TransitionToJson(transition));
    }
    root["transitions"] = transitions;

    if (!project.markers.empty()) {
        Json::Value markers(Json::arrayValue);
        for (const Marker& marker : project.markers) {
            Json::Value mj(Json::objectValue);
            mj["time"] = marker.time;
            mj["name"] = marker.name;
            Json::Value color(Json::arrayValue);
            for (const std::uint8_t component : marker.color) {
                color.append(static_cast<int>(component));
            }
            mj["color"] = color;
            markers.append(mj);
        }
        root["markers"] = markers;
    }
    if (project.work_out > project.work_in) {
        root["work_in"] = project.work_in;
        root["work_out"] = project.work_out;
    }
    if (project.master_gain_db != 0.0f) {
        root["master_gain_db"] = static_cast<double>(project.master_gain_db);
    }
    // Proxy workflow — always written so the project's decode-mode
    // round-trips even at non-defaults.
    root["use_proxies"] = project.use_proxies;
    root["proxy_ingest"] = project.proxy_ingest;
    root["proxy_max_dim"] = project.proxy_max_dim;
    root["proxy_crf"] = project.proxy_crf;
    root["proxy_cache_max_mb"] = project.proxy_cache_max_mb;
    return root;
}

}  // namespace

// Transition type <-> wire name — exported (project_store.h): the
// ve.core v3 service table consumes the same tokens the .xerproj
// vocabulary uses, so both sides share this table.
const char* TransitionTypeToString(Transition::Type type) {
    switch (type) {
        case Transition::Type::Crossfade:
            return "crossfade";
        case Transition::Type::WipeLeft:
            return "wipe_left";
        case Transition::Type::WipeRight:
            return "wipe_right";
        case Transition::Type::WipeUp:
            return "wipe_up";
        case Transition::Type::WipeDown:
            return "wipe_down";
        case Transition::Type::SlideLeft:
            return "slide_left";
        case Transition::Type::SlideRight:
            return "slide_right";
        case Transition::Type::SlideUp:
            return "slide_up";
        case Transition::Type::SlideDown:
            return "slide_down";
        case Transition::Type::DipToBlack:
            return "dip_to_black";
        case Transition::Type::DipToWhite:
            return "dip_to_white";
        case Transition::Type::AdditiveDissolve:
            return "additive_dissolve";
        case Transition::Type::Script:
            return "script";
    }
    return "crossfade";  // unreachable
}

bool TransitionTypeFromString(const std::string& text,
                              Transition::Type* out) {
    static const struct {
        const char* name;
        Transition::Type type;
    } kTypes[] = {
        {"crossfade", Transition::Type::Crossfade},
        {"wipe_left", Transition::Type::WipeLeft},
        {"wipe_right", Transition::Type::WipeRight},
        {"wipe_up", Transition::Type::WipeUp},
        {"wipe_down", Transition::Type::WipeDown},
        {"slide_left", Transition::Type::SlideLeft},
        {"slide_right", Transition::Type::SlideRight},
        {"slide_up", Transition::Type::SlideUp},
        {"slide_down", Transition::Type::SlideDown},
        {"dip_to_black", Transition::Type::DipToBlack},
        {"dip_to_white", Transition::Type::DipToWhite},
        {"additive_dissolve", Transition::Type::AdditiveDissolve},
        {"script", Transition::Type::Script},
    };
    for (const auto& entry : kTypes) {
        if (text == entry.name) {
            *out = entry.type;
            return true;
        }
    }
    return false;
}

bool ProjectStore::Save(const Project& project,
                        const std::string& utf8_path,
                        std::string* error_out) {
    if (utf8_path.empty()) {
        return Fail(error_out, "empty project path");
    }

    const Json::Value root = ProjectToJson(project);
    Json::StreamWriterBuilder writer_builder;
    writer_builder["indentation"] = "";  // compact single-line JSON
    const std::string text = Json::writeString(writer_builder, root);

    // UTF-8 path -> native fs::path so non-ASCII .xerproj names work.
    const std::filesystem::path target = Path::FromUtf8(utf8_path);
    std::filesystem::path tmp = target;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return Fail(error_out, "cannot open " + Path::ToUtf8(tmp) +
                                     " for writing");
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out.good()) {
            return Fail(error_out,
                        "failed writing " + Path::ToUtf8(tmp));
        }
    }  // destructor closes the file before the rename (required on Windows)

    // Crash-safe publish: atomic replace — no window where neither the
    // old project nor its replacement exists. On failure `target` is
    // untouched and `tmp` stays on disk for recovery — deliberately
    // NEVER deleted (removing it after a failed publish would destroy
    // the only remaining copy of the user's data).
    return Path::AtomicReplace(tmp, target, error_out);
}

bool ProjectStore::Load(const std::string& utf8_path,
                        Project* project_out, std::string* error_out) {
    if (project_out == nullptr) {
        return Fail(error_out, "null project_out");
    }
    if (utf8_path.empty()) {
        return Fail(error_out, "empty project path");
    }

    std::ifstream in(Path::FromUtf8(utf8_path), std::ios::binary);
    if (!in.is_open()) {
        return Fail(error_out, "cannot open " + utf8_path);
    }

    Json::CharReaderBuilder reader_builder;
    Json::Value root;
    std::string parse_errors;
    bool parsed = false;
    try {
        parsed = Json::parseFromStream(reader_builder, in, &root,
                                       &parse_errors);
    } catch (...) {
        // jsoncpp throws Json::RuntimeError when the input exceeds the
        // parser stack limit (deeply nested JSON ~>1000 levels). The
        // store contract is never-throw — a crafted .xerproj must not
        // escape as an exception into a noexcept UI caller.
        return Fail(error_out,
                    "invalid JSON in " + utf8_path +
                        ": document too deeply nested or malformed");
    }
    if (!parsed) {
        return Fail(error_out, "invalid JSON in " + utf8_path + ": " +
                                 parse_errors);
    }
    return ProjectFromJson(root, project_out, error_out);
}

// ---- ve.core v3 describe layer (project_store.h) ---------------------
// JSON projections of live editor state for the ve_call service
// table — kept next to the .xerproj serializer so the served shapes
// and the file shapes evolve together (same wire names everywhere).

Json::Value VeDescribeProject(const Project& project,
                              const std::string& project_path,
                              bool dirty) {
    Json::Value v(Json::objectValue);
    v["width"] = project.width;
    v["height"] = project.height;
    v["fps_num"] = project.fps_num;
    v["fps_den"] = project.fps_den;
    Json::Value bg(Json::arrayValue);
    for (const std::uint8_t c : project.bg_color) {
        bg.append(Json::UInt(c));
    }
    v["bg_color"] = std::move(bg);
    v["duration"] = project.duration();
    v["tracks"] = static_cast<Json::UInt64>(project.tracks.size());
    v["clips"] = static_cast<Json::UInt64>(project.clips.size());
    v["transitions"] =
        static_cast<Json::UInt64>(project.transitions.size());
    v["markers"] = static_cast<Json::UInt64>(project.markers.size());
    v["work_in"] = project.work_in;
    v["work_out"] = project.work_out;
    v["master_gain_db"] = static_cast<double>(project.master_gain_db);
    v["path"] = project_path;
    v["untitled"] = project_path.empty();
    v["dirty"] = dirty;
    return v;
}

Json::Value VeDescribeTimeline(const Project& project, double playhead,
                               const std::string& selected_clip_id) {
    Json::Value v(Json::objectValue);
    v["playhead"] = playhead;
    v["duration"] = project.duration();
    v["tracks"] = static_cast<Json::UInt64>(project.tracks.size());
    v["clips"] = static_cast<Json::UInt64>(project.clips.size());
    v["transitions"] =
        static_cast<Json::UInt64>(project.transitions.size());
    v["markers"] = static_cast<Json::UInt64>(project.markers.size());
    v["selected_clip"] = selected_clip_id;
    v["work_in"] = project.work_in;
    v["work_out"] = project.work_out;
    v["fps_num"] = project.fps_num;
    v["fps_den"] = project.fps_den;
    v["frame_seconds"] =
        project.fps_num > 0
            ? static_cast<double>(project.fps_den) / project.fps_num
            : 1.0 / 30.0;
    return v;
}

Json::Value VeDescribeTracks(const Project& project) {
    Json::Value tracks(Json::arrayValue);
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        const Track& t = project.tracks[i];
        Json::Value j(Json::objectValue);
        j["index"] = static_cast<Json::UInt64>(i);
        j["kind"] = TrackKindToString(t.kind);
        j["name"] = t.name;
        j["muted"] = t.muted;
        j["hidden"] = t.hidden;
        j["locked"] = t.locked;
        j["gain_db"] = static_cast<double>(t.gain_db);
        j["pan"] = static_cast<double>(t.pan);
        std::size_t count = 0;
        for (const Clip& c : project.clips) {
            count += c.track == static_cast<int>(i) ? 1 : 0;
        }
        j["clips"] = static_cast<Json::UInt64>(count);
        tracks.append(std::move(j));
    }
    Json::Value v(Json::objectValue);
    v["tracks"] = std::move(tracks);
    v["count"] = static_cast<Json::UInt64>(project.tracks.size());
    return v;
}

Json::Value VeDescribeClip(const Project& project,
                           const std::string& clip_id) {
    const Clip* c = FindClip(project, clip_id);
    if (c == nullptr) return Json::Value(Json::objectValue);
    Json::Value j(Json::objectValue);
    j["id"] = c->id;
    j["name"] = c->name;
    j["track"] = c->track;
    j["start"] = c->timeline_start;
    j["end"] = ClipEndSeconds(*c);
    j["duration"] = c->duration;
    j["in_point"] = c->in_point;
    j["rate"] = c->playback_rate;
    j["source"] = c->source_path;
    j["muted"] = c->muted;
    j["gain_db"] = static_cast<double>(c->gain_db);
    j["pan"] = static_cast<double>(c->pan);
    j["fade_in"] = c->fade_in;
    j["fade_out"] = c->fade_out;
    j["link_group"] = c->link_group;
    j["adjustment"] = c->is_adjustment;
    switch (c->source.kind) {
        case SourceKind::Media:      j["kind"] = "media"; break;
        case SourceKind::ColorMatte: j["kind"] = "color"; break;
        case SourceKind::ColorBars:  j["kind"] = "bars";  break;
        case SourceKind::Text:       j["kind"] = "text";  break;
    }
    if (c->source.kind != SourceKind::Media) {
        j["param"] = c->source.param;
    }
    j["effects"] = static_cast<Json::UInt64>(c->effects.size());
    j["audio_fx"] = static_cast<Json::UInt64>(c->audio_fx.size());
    return j;
}

Json::Value VeDescribeClips(const Project& project) {
    Json::Value clips(Json::arrayValue);
    for (const Clip& c : project.clips) {
        clips.append(VeDescribeClip(project, c.id));
    }
    Json::Value v(Json::objectValue);
    v["clips"] = std::move(clips);
    v["count"] = static_cast<Json::UInt64>(project.clips.size());
    return v;
}

Json::Value VeDescribeTransitions(const Project& project) {
    Json::Value transitions(Json::arrayValue);
    for (const Transition& t : project.transitions) {
        Json::Value j(Json::objectValue);
        j["from"] = t.from_clip;
        j["to"] = t.to_clip;
        j["type"] = TransitionTypeToString(t.type);
        j["duration"] = t.duration;
        if (!t.script.empty()) j["script"] = t.script;
        transitions.append(std::move(j));
    }
    Json::Value v(Json::objectValue);
    v["transitions"] = std::move(transitions);
    v["count"] = static_cast<Json::UInt64>(project.transitions.size());
    return v;
}

Json::Value VeDescribeMarkers(const Project& project) {
    Json::Value markers(Json::arrayValue);
    for (const Marker& m : project.markers) {
        Json::Value j(Json::objectValue);
        j["time"] = m.time;
        j["name"] = m.name;
        char color[16];
        std::snprintf(color, sizeof color, "#%02x%02x%02x%02x",
                      m.color[0], m.color[1], m.color[2], m.color[3]);
        j["color"] = color;
        markers.append(std::move(j));
    }
    Json::Value v(Json::objectValue);
    v["markers"] = std::move(markers);
    v["count"] = static_cast<Json::UInt64>(project.markers.size());
    return v;
}

Json::Value VeDescribeCatalog(const std::vector<CatalogEntry>& entries) {
    Json::Value list(Json::arrayValue);
    for (const CatalogEntry& e : entries) {
        Json::Value j(Json::objectValue);
        j["id"] = Json::Int64(e.id);
        j["path"] = e.path;
        j["duration"] = e.duration_seconds;
        j["width"] = e.width;
        j["height"] = e.height;
        j["fps"] = e.fps;
        j["has_video"] = e.HasVideo();
        j["has_audio"] = e.has_audio;
        j["video_codec"] = e.video_codec;
        j["audio_codec"] = e.audio_codec;
        j["file_size"] = Json::Int64(e.file_size_bytes);
        j["poster"] = e.poster_path;
        list.append(std::move(j));
    }
    Json::Value v(Json::objectValue);
    v["entries"] = std::move(list);
    v["count"] = static_cast<Json::UInt64>(entries.size());
    return v;
}

const std::vector<std::string>& VeServiceNames() {
    // Keep in EXACT sync with DispatchExtHostCall in
    // ui/video_editor_app.cc — the dispatcher rejects anything not
    // listed here with "unknown_call". v2 aliases (project.clips,
    // status) are intentionally absent: still served, not advertised.
    static const std::vector<std::string> names = {
        "ve.services",
        "app.info",
        // project.*
        "project.info", "project.path", "project.new", "project.open",
        "project.save", "project.settings", "project.markers",
        "project.add_marker", "project.remove_marker",
        "project.rename_marker", "project.set_work_area",
        // timeline.*
        "timeline.info", "timeline.tracks", "timeline.clips",
        "timeline.transitions", "timeline.add_track",
        "timeline.remove_track", "timeline.rename_track",
        "timeline.set_track", "timeline.add_clip", "timeline.move_clip",
        "timeline.remove_clip", "timeline.trim_clip",
        "timeline.split_clip", "timeline.ripple_delete",
        "timeline.lift_range", "timeline.slip", "timeline.slide",
        "timeline.set_rate", "timeline.nudge", "timeline.select_clip",
        "timeline.add_transition", "timeline.remove_transition",
        "timeline.set_clip", "timeline.set_keyframes",
        // transport.*
        "transport.info", "transport.play", "transport.pause",
        "transport.stop", "transport.seek", "transport.frame_step",
        "transport.set_loop", "transport.set_rate", "transport.shuttle",
        "transport.shuttle_stop", "transport.goto_marker",
        "transport.set_quality",
        // selection.*
        "selection.get", "selection.set", "selection.clear",
        // media.*
        "media.list", "media.import", "media.refresh", "media.select",
        "media.add_to_timeline", "media.bounds",
        // fx.*
        "fx.list", "fx.add", "fx.remove", "fx.clear",
        // export.*
        "export.presets", "export.start", "export.cancel",
        "export.status",
        // ui.*
        "ui.status", "ui.open_panel", "ui.list_extensions",
        "ui.open_extension_ui", "ui.run_extension",
        "ui.rescan_extensions",
        // edit.*
        "edit.undo", "edit.redo", "edit.history",
        // ai.* — the editor's analysis jobs (beats / whisper /
        // find-objects) plus the sync compute services (silences,
        // scenes, loudness) exposed to extensions.
        "ai.status", "ai.last", "ai.cancel",
        "ai.detect_beats", "ai.transcribe", "ai.find_objects",
        "ai.detect_silences", "ai.detect_scenes", "ai.loudness",
    };
    return names;
}

bool VeServiceKnown(const std::string& name) {
    const auto& names = VeServiceNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

}  // namespace XER::VideoEditor
