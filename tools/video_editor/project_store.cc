// .xerproj JSON v1 load/save — see project_store.h for the contract.
// Serialization is a straight mapping onto the spec key layout
// (version / canvas / tracks / clips / transitions); validation is
// strict for primary data and self-healing for derived data
// (transitions), matching the timeline model's invariants.
#include "project_store.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#include <json/json.h>

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
    };
    for (const auto& entry : kTypes) {
        if (text == entry.name) {
            *out = entry.type;
            return true;
        }
    }
    return false;
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
        if (version != ProjectStore::kVersion) {
            return Fail(error_out,
                        "unsupported project version " +
                            std::to_string(version) + " (expected " +
                            std::to_string(ProjectStore::kVersion) + ")");
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
            if (clip.source_path.empty()) {
                return Fail(error_out,
                            where + ".source: required non-empty");
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
                !r.GetF32("gain_db", clip.gain_db, &clip.gain_db) ||
                !r.GetF64("fade_in", clip.fade_in, &clip.fade_in) ||
                !r.GetF64("fade_out", clip.fade_out, &clip.fade_out)) {
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
    if (!Json::parseFromStream(reader_builder, in, &root, &parse_errors)) {
        return Fail(error_out, "invalid JSON in " + utf8_path + ": " +
                                 parse_errors);
    }
    return ProjectFromJson(root, project_out, error_out);
}

}  // namespace XER::VideoEditor
