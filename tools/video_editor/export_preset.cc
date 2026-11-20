// Export preset model + JSON persistence — see export_preset.h for the
// contract (audio-only convention, load tolerance, file shape).
#include "export_preset.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include <json/json.h>

#include "helper/env_utils.h"
#include "helper/path_utils.h"

namespace XER::VideoEditor {
namespace {

namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers

constexpr std::int64_t kPresetsVersion = 1;

bool Fail(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

// Per-entry sanitation on load: keeps a file-authored preset inside
// the same envelope the builtins live in. Bitrates clamp at 0
// (negative has no meaning; 0 stays the "bridge default" sentinel) and
// the scale clamps to (0, 1] — presets never upscale.
ExportPreset SanitizePreset(ExportPreset preset) {
    if (preset.video_bit_rate < 0) preset.video_bit_rate = 0;
    if (preset.audio_bit_rate < 0) preset.audio_bit_rate = 0;
    if (!std::isfinite(preset.resolution_scale) ||
        preset.resolution_scale <= 0.0 ||
        preset.resolution_scale > 1.0) {
        preset.resolution_scale = 1.0;
    }
    return preset;
}

Json::Value PresetToJson(const ExportPreset& preset) {
    Json::Value out(Json::objectValue);
    out["name"] = preset.name;
    out["video_codec"] = preset.video_codec;
    out["video_bit_rate"] = preset.video_bit_rate;
    out["audio_codec"] = preset.audio_codec;
    out["audio_bit_rate"] = preset.audio_bit_rate;
    out["resolution_scale"] = preset.resolution_scale;
    return out;
}

// Lenient single-entry decode: absent keys take the struct defaults, a
// PRESENT key of the wrong JSON type rejects the whole entry (the
// caller skips it) — same strictness convention as project_store.cc's
// Reader. Returns false for non-object input or a missing/empty name.
bool PresetFromJson(const Json::Value& json, ExportPreset* preset_out) {
    if (!json.isObject()) {
        return false;
    }
    ExportPreset preset;
    const Json::Value& name = json["name"];
    if (!name.isString() || name.asString().empty()) {
        return false;  // a preset without a name is useless in the UI
    }
    preset.name = name.asString();

    const Json::Value& vcodec = json["video_codec"];
    if (vcodec.isString()) {
        preset.video_codec = vcodec.asString();
    } else if (!vcodec.isNull()) {
        return false;
    }
    const Json::Value& vbrate = json["video_bit_rate"];
    if (vbrate.isInt()) {
        preset.video_bit_rate = vbrate.asInt();
    } else if (!vbrate.isNull()) {
        return false;
    }
    const Json::Value& acodec = json["audio_codec"];
    if (acodec.isString()) {
        preset.audio_codec = acodec.asString();
    } else if (!acodec.isNull()) {
        return false;
    }
    const Json::Value& abrate = json["audio_bit_rate"];
    if (abrate.isInt()) {
        preset.audio_bit_rate = abrate.asInt();
    } else if (!abrate.isNull()) {
        return false;
    }
    const Json::Value& scale = json["resolution_scale"];
    if (scale.isNumeric()) {
        preset.resolution_scale = scale.asDouble();
    } else if (!scale.isNull()) {
        return false;
    }
    *preset_out = SanitizePreset(std::move(preset));
    return true;
}

}  // namespace

std::vector<ExportPreset> BuiltinPresets() {
    std::vector<ExportPreset> presets;

    ExportPreset draft;
    draft.name = "Draft 720p";
    draft.video_codec = "libx264";
    draft.video_bit_rate = 4000000;
    draft.audio_codec = "aac";
    draft.audio_bit_rate = 128000;
    draft.resolution_scale = 0.5;
    presets.push_back(draft);

    ExportPreset youtube;
    youtube.name = "YouTube 1080p";
    youtube.video_codec = "libx264";
    youtube.video_bit_rate = 8000000;
    youtube.audio_codec = "aac";
    youtube.audio_bit_rate = 192000;
    youtube.resolution_scale = 1.0;
    presets.push_back(youtube);

    ExportPreset high;
    high.name = "High Quality";
    high.video_codec = "libx264";
    high.video_bit_rate = 20000000;
    high.audio_codec = "aac";
    high.audio_bit_rate = 256000;
    high.resolution_scale = 1.0;
    presets.push_back(high);

    ExportPreset audio_only;
    audio_only.name = "Audio only AAC";
    audio_only.video_codec.clear();   // audio-only convention
    audio_only.video_bit_rate = 0;
    audio_only.audio_codec = "aac";
    audio_only.audio_bit_rate = 192000;
    audio_only.resolution_scale = 1.0;
    presets.push_back(audio_only);

    return presets;
}

void ApplyPreset(const ExportPreset& preset, ExportOptions* options) {
    if (options == nullptr) {
        return;
    }
    options->video_codec = preset.video_codec;
    options->video_bit_rate = preset.video_bit_rate;
    options->audio_codec = preset.audio_codec;
    options->audio_bit_rate = preset.audio_bit_rate;
    options->resolution_scale = preset.resolution_scale;
}

std::string DefaultPresetsPath() {
    // Same UTF-8 safe env chain as MediaCatalog::DefaultDbPath:
    // LOCALAPPDATA arrives ANSI on non-EN Windows -> NormalizeUtf8Path;
    // XDG_DATA_HOME / HOME/.local/share cover Linux/macOS; temp dir is
    // the last resort. Generic '/' separators are appended — Windows
    // APIs accept them.
    std::string base =
        Path::NormalizeUtf8Path(XER::Helper::GetEnv("LOCALAPPDATA"));
    if (base.empty()) {
        base =
            Path::NormalizeUtf8Path(XER::Helper::GetEnv("XDG_DATA_HOME"));
    }
    if (base.empty()) {
        const std::string home =
            Path::NormalizeUtf8Path(XER::Helper::GetEnv("HOME"));
        if (!home.empty()) {
            base = home + "/.local/share";
        }
    }
    if (base.empty()) {
        std::error_code ec;
        base = Path::ToUtf8(std::filesystem::temp_directory_path(ec));
    }
    if (base.empty()) {
        base = ".";
    }
    return base + "/XER/VideoEditor/export_presets.json";
}

bool SavePresets(const std::vector<ExportPreset>& presets,
                 const std::string& utf8_path, std::string* error_out) {
    if (utf8_path.empty()) {
        return Fail(error_out, "empty presets path");
    }

    Json::Value root(Json::objectValue);
    root["version"] = Json::Int64(kPresetsVersion);
    Json::Value list(Json::arrayValue);
    for (const ExportPreset& preset : presets) {
        if (preset.name.empty()) {
            continue;  // unnamed presets can never be picked — drop
        }
        list.append(PresetToJson(preset));
    }
    root["presets"] = std::move(list);

    Json::StreamWriterBuilder writer_builder;
    writer_builder["indentation"] = "";  // compact single-line JSON
    const std::string text = Json::writeString(writer_builder, root);

    const std::filesystem::path target = Path::FromUtf8(utf8_path);
    const std::filesystem::path parent = target.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return Fail(error_out,
                        "cannot create " + Path::ToUtf8(parent) + ": " +
                            ec.message());
        }
    }
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
    }  // destructor closes the file before the rename (Windows)

    // Crash-safe publish: atomic replace — no window where neither the
    // old presets nor their replacement exist. On failure `target` is
    // untouched and `tmp` stays on disk for recovery — deliberately
    // NEVER deleted (removing it after a failed publish would destroy
    // the only remaining copy of the user's data).
    return Path::AtomicReplace(tmp, target, error_out);
}

bool LoadPresets(const std::string& utf8_path,
                 std::vector<ExportPreset>* presets_out,
                 std::string* error_out) {
    if (presets_out == nullptr) {
        return Fail(error_out, "null presets_out");
    }
    presets_out->clear();
    if (utf8_path.empty()) {
        return Fail(error_out, "empty presets path");
    }

    const std::filesystem::path source = Path::FromUtf8(utf8_path);
    {
        std::error_code ec;
        if (!std::filesystem::exists(source, ec)) {
            return true;  // first run: no user presets yet — not an error
        }
    }

    std::ifstream in(source, std::ios::binary);
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
        // Same contract as ProjectStore::Load: never-throw — a crafted
        // deeply-nested file must not escape as an exception.
        return Fail(error_out,
                    "invalid JSON in " + utf8_path +
                        ": document too deeply nested or malformed");
    }
    if (!parsed) {
        return Fail(error_out, "invalid JSON in " + utf8_path + ": " +
                                   parse_errors);
    }
    if (!root.isObject()) {
        return Fail(error_out,
                    "presets root is not a JSON object in " + utf8_path);
    }
    if (root.isMember("presets") && !root["presets"].isArray()) {
        return Fail(error_out,
                    "presets: expected an array in " + utf8_path);
    }

    if (root.isMember("presets")) {
        for (const Json::Value& entry : root["presets"]) {
            ExportPreset preset;
            // Malformed entries are skipped — one bad hand-edited
            // preset must not kill the rest of the list.
            if (PresetFromJson(entry, &preset)) {
                presets_out->push_back(std::move(preset));
            }
        }
    }
    return true;
}

}  // namespace XER::VideoEditor
