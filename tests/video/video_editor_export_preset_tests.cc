// Export-preset suite — core-only coverage for export_preset.h:
// BuiltinPresets() sanity, ApplyPreset() field mapping, and the
// SavePresets()/LoadPresets() JSON round-trip including malformed
// input tolerance. No FFmpeg needed; plain-assert style via
// video_editor_test_util.h.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "video_editor_test_util.h"
#include "export_engine.h"
#include "export_preset.h"
#include "helper/path_utils.h"

namespace {
namespace Path = XER::Helper::Path;
using XER::VideoEditor::ExportOptions;
using XER::VideoEditor::ExportPreset;

bool PresetsEqual(const ExportPreset& a, const ExportPreset& b) {
    return a.name == b.name && a.video_codec == b.video_codec &&
           a.video_bit_rate == b.video_bit_rate &&
           a.audio_codec == b.audio_codec &&
           a.audio_bit_rate == b.audio_bit_rate &&
           a.resolution_scale == b.resolution_scale;
}

std::filesystem::path PresetTempDir() {
    const auto dir =
        XER::VideoEditor::Test::TempDir() / "export_presets";
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteTextFile(const std::filesystem::path& path,
                   const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

}  // namespace

int XER_VideoEditor_RunExportPresetTests() {
    using XER::VideoEditor::ApplyPreset;
    using XER::VideoEditor::BuiltinPresets;
    using XER::VideoEditor::DefaultPresetsPath;
    using XER::VideoEditor::LoadPresets;
    using XER::VideoEditor::SavePresets;

    // -- BuiltinPresets sanity ---------------------------------------
    const std::vector<ExportPreset> builtins = BuiltinPresets();
    XER_VE_CHECK(builtins.size() >= 3);
    {
        std::vector<std::string> names;
        bool saw_audio_only = false;
        bool saw_scaled = false;
        int prev_video_rate = -1;
        bool monotone = true;
        for (const ExportPreset& preset : builtins) {
            XER_VE_CHECK(!preset.name.empty());
            names.push_back(preset.name);
            // Every preset must carry a usable audio codec; video
            // presets must also carry a video codec.
            XER_VE_CHECK(!preset.audio_codec.empty());
            XER_VE_CHECK(preset.audio_bit_rate > 0);
            XER_VE_CHECK(preset.resolution_scale > 0.0 &&
                         preset.resolution_scale <= 1.0);
            if (preset.IsAudioOnly()) {
                saw_audio_only = true;
                XER_VE_CHECK(preset.video_bit_rate == 0);
            } else {
                XER_VE_CHECK(!preset.video_codec.empty());
                XER_VE_CHECK(preset.video_bit_rate > 0);
                // Factory list is ordered low -> high quality across
                // the video presets (the audio-only entry sits apart).
                if (prev_video_rate >= 0 &&
                    preset.video_bit_rate <= prev_video_rate) {
                    monotone = false;
                }
                prev_video_rate = preset.video_bit_rate;
                if (preset.resolution_scale < 1.0) saw_scaled = true;
            }
        }
        XER_VE_CHECK(monotone);
        XER_VE_CHECK(saw_audio_only);
        XER_VE_CHECK(saw_scaled);
        XER_VE_CHECK(
            std::adjacent_find(names.begin(), names.end()) ==
                names.end() ||
            names.size() < 2);
        std::vector<std::string> sorted = names;
        std::sort(sorted.begin(), sorted.end());
        XER_VE_CHECK(std::adjacent_find(sorted.begin(), sorted.end()) ==
                     sorted.end());  // unique names
    }

    // -- ApplyPreset field mapping ------------------------------------
    {
        ExportOptions options;
        options.output_path = "keep.mp4";
        options.range_in = 1.5;
        options.range_out = 3.0;
        const ExportPreset& preset = builtins.front();
        ApplyPreset(preset, &options);
        XER_VE_CHECK(options.video_codec == preset.video_codec);
        XER_VE_CHECK(options.video_bit_rate == preset.video_bit_rate);
        XER_VE_CHECK(options.audio_codec == preset.audio_codec);
        XER_VE_CHECK(options.audio_bit_rate == preset.audio_bit_rate);
        XER_VE_CHECK(options.resolution_scale ==
                     preset.resolution_scale);
        // Untouched fields.
        XER_VE_CHECK(options.output_path == "keep.mp4");
        XER_VE_CHECK(options.range_in == 1.5);
        XER_VE_CHECK(options.range_out == 3.0);
    }
    {
        // Audio-only convention propagates verbatim.
        ExportPreset audio_only;
        audio_only.name = "podcast";
        audio_only.video_codec.clear();
        audio_only.video_bit_rate = 0;
        audio_only.audio_codec = "aac";
        audio_only.audio_bit_rate = 256000;
        audio_only.resolution_scale = 1.0;
        ExportOptions options;
        ApplyPreset(audio_only, &options);
        XER_VE_CHECK(options.video_codec.empty());
        XER_VE_CHECK(options.video_bit_rate == 0);
        XER_VE_CHECK(options.audio_codec == "aac");
        XER_VE_CHECK(options.audio_bit_rate == 256000);
        // Null options is a documented no-op — must not crash.
        ApplyPreset(audio_only, nullptr);
    }

    // -- Save/Load round-trip -----------------------------------------
    const auto dir = PresetTempDir();
    std::string err;
    {
        const auto file = dir / "roundtrip.json";
        std::vector<ExportPreset> presets;
        ExportPreset custom;
        custom.name = "Web 720p";
        custom.video_codec = "libx264";
        custom.video_bit_rate = 5500000;
        custom.audio_codec = "aac";
        custom.audio_bit_rate = 160000;
        custom.resolution_scale = 0.75;
        presets.push_back(custom);
        ExportPreset audio;
        audio.name = "Audio only opus";
        audio.video_codec.clear();
        audio.video_bit_rate = 0;
        audio.audio_codec = "libopus";
        audio.audio_bit_rate = 96000;
        audio.resolution_scale = 1.0;
        presets.push_back(audio);
        ExportPreset unnamed;  // dropped by SavePresets (empty name)
        unnamed.name.clear();
        presets.push_back(unnamed);

        XER_VE_CHECK(
            SavePresets(presets, Path::ToUtf8(file), &err));
        XER_VE_CHECK(std::filesystem::exists(file));

        std::vector<ExportPreset> loaded;
        XER_VE_CHECK(
            LoadPresets(Path::ToUtf8(file), &loaded, &err));
        XER_VE_CHECK(loaded.size() == 2);
        if (loaded.size() == 2) {
            XER_VE_CHECK(PresetsEqual(loaded[0], custom));
            XER_VE_CHECK(PresetsEqual(loaded[1], audio));
            XER_VE_CHECK(loaded[1].IsAudioOnly());
        }
    }

    // -- missing file -> true + empty (first run is not an error) -----
    {
        std::vector<ExportPreset> loaded;
        const auto missing = dir / "does_not_exist.json";
        std::error_code ec;
        std::filesystem::remove(missing, ec);
        XER_VE_CHECK(
            LoadPresets(Path::ToUtf8(missing), &loaded, &err));
        XER_VE_CHECK(loaded.empty());
    }

    // -- malformed input tolerance ------------------------------------
    {
        // Unparseable JSON -> false + error, never throws.
        const auto file = dir / "garbage.json";
        WriteTextFile(file, "{ this is not json ]]]");
        std::vector<ExportPreset> loaded;
        XER_VE_CHECK(
            !LoadPresets(Path::ToUtf8(file), &loaded, &err));
        XER_VE_CHECK(!err.empty());
    }
    {
        // Valid JSON, wrong top-level shape -> false.
        const auto file = dir / "array_root.json";
        WriteTextFile(file, "[1, 2, 3]");
        std::vector<ExportPreset> loaded;
        XER_VE_CHECK(
            !LoadPresets(Path::ToUtf8(file), &loaded, &err));
    }
    {
        // "presets" member of the wrong type -> false.
        const auto file = dir / "bad_member.json";
        WriteTextFile(file, "{\"version\":1,\"presets\":42}");
        std::vector<ExportPreset> loaded;
        XER_VE_CHECK(
            !LoadPresets(Path::ToUtf8(file), &loaded, &err));
    }
    {
        // Mixed-validity entries: good ones survive, bad ones are
        // skipped; out-of-range values sanitize.
        const auto file = dir / "mixed.json";
        WriteTextFile(
            file,
            "{\"version\":1,\"presets\":["
            "{\"name\":\"ok\",\"video_codec\":\"libx264\","
            "\"video_bit_rate\":1000000,\"audio_codec\":\"aac\","
            "\"audio_bit_rate\":128000,\"resolution_scale\":0.5},"
            "42,"
            "{\"video_codec\":\"libx264\"},"
            "{\"name\":\"bad type\",\"video_bit_rate\":\"lots\"},"
            "{\"name\":\"clamped\",\"video_bit_rate\":-5,"
            "\"resolution_scale\":3.0}]}");
        std::vector<ExportPreset> loaded;
        XER_VE_CHECK(
            LoadPresets(Path::ToUtf8(file), &loaded, &err));
        XER_VE_CHECK(loaded.size() == 2);
        if (loaded.size() == 2) {
            XER_VE_CHECK(loaded[0].name == "ok");
            XER_VE_CHECK(loaded[0].video_bit_rate == 1000000);
            XER_VE_CHECK(loaded[1].name == "clamped");
            XER_VE_CHECK(loaded[1].video_bit_rate == 0);
            XER_VE_CHECK(loaded[1].resolution_scale == 1.0);
        }
    }

    // -- path helper ---------------------------------------------------
    XER_VE_CHECK(!DefaultPresetsPath().empty());
    XER_VE_CHECK(DefaultPresetsPath().find("export_presets.json") !=
                 std::string::npos);

    // Empty path / null out-param are contract violations, not crashes.
    {
        std::vector<ExportPreset> loaded;
        XER_VE_CHECK(!LoadPresets("", &loaded, &err));
        XER_VE_CHECK(!LoadPresets("x.json", nullptr, nullptr));
        XER_VE_CHECK(!SavePresets(builtins, "", &err));
    }

    return XER::VideoEditor::Test::Failures();
}
