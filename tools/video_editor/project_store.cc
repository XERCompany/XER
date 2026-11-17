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
#include <filesystem>
#include <fstream>
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
    };
    for (const auto& entry : kTypes) {
        if (text == entry.name) {
            *out = entry.type;
            return true;
        }
    }
    return false;
}

const char* BlendModeToString(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: return "normal";
        case BlendMode::Add: return "add";
        case BlendMode::Multiply: return "multiply";
        case BlendMode::Screen: return "screen";
        case BlendMode::Overlay: return "overlay";
        case BlendMode::Darken: return "darken";
        case BlendMode::Lighten: return "lighten";
    }
    return "normal";  // unreachable
}

bool BlendModeFromString(const std::string& text, BlendMode* out) {
    static const struct {
        const char* name;
        BlendMode mode;
    } kModes[] = {
        {"normal", BlendMode::Normal},
        {"add", BlendMode::Add},
        {"multiply", BlendMode::Multiply},
        {"screen", BlendMode::Screen},
        {"overlay", BlendMode::Overlay},
        {"darken", BlendMode::Darken},
        {"lighten", BlendMode::Lighten},
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
                !r.GetF32("gain_db", track.gain_db, &track.gain_db)) {
                return false;
            }
            project.tracks.push_back(track);
        }
    }

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
            Reader r{cj, where, error_out};
            Clip clip;

            if (!r.GetStr("id", "", &clip.id)) {
                return false;
            }
            if (clip.id.empty()) {
                return Fail(error_out, where + ".id: required non-empty");
            }
            if (FindClip(project, clip.id) != nullptr) {
                return Fail(error_out,
                            where + ".id: duplicate clip id '" + clip.id +
                                "'");
            }
            if (!r.GetStr("source", "", &clip.source_path)) {
                return false;
            }

            std::int64_t track_index = 0;
            if (!r.GetI64("track", 0, &track_index)) {
                return false;
            }
            if (track_index < 0 ||
                track_index >=
                    static_cast<std::int64_t>(project.tracks.size())) {
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
                !clip.is_adjustment && clip.source_path.empty()) {
                return Fail(error_out,
                            where + ".source: required non-empty");
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

            project.clips.push_back(clip);
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
        std::sort(project.markers.begin(), project.markers.end(),
                  [](const Marker& a, const Marker& b) {
                      return a.time < b.time;
                  });
    }

    {
        Reader r{root, "project", error_out};
        if (!r.GetF64("work_in", project.work_in, &project.work_in) ||
            !r.GetF64("work_out", project.work_out, &project.work_out)) {
            return false;
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
    return out;
}

Json::Value ClipToJson(const Clip& clip) {
    Json::Value out(Json::objectValue);
    out["id"] = clip.id;
    out["source"] = clip.source_path;
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
    return root;
}

}  // namespace

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

    // Crash-safe publish: rename over the target. Windows rename()
    // refuses to overwrite — remove the target and retry once.
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::error_code remove_ec;
        std::filesystem::remove(target, remove_ec);
        std::filesystem::rename(tmp, target, ec);
        if (ec) {
            std::error_code cleanup_ec;
            std::filesystem::remove(tmp, cleanup_ec);
            return Fail(error_out,
                        "cannot rename " + Path::ToUtf8(tmp) + " to " +
                            Path::ToUtf8(target) + ": " + ec.message());
        }
    }
    return true;
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

}  // namespace XER::VideoEditor
