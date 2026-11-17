// Named export presets for the VideoEditor core (Phase 2). UI-free:
// BuiltinPresets() ships the factory set, ApplyPreset() stamps a preset
// onto ExportOptions, and SavePresets()/LoadPresets() persist user
// presets as JSON under the per-user editor data dir (same env-chain
// convention as MediaCatalog::DefaultDbPath).
#pragma once

#include <string>
#include <vector>

#include "export_engine.h"

namespace XER::VideoEditor {

// One named bundle of encoder settings surfaced in the export dialog's
// Preset section.
//
// Audio-only convention: a preset whose `video_codec` is EMPTY encodes
// "audio only". ApplyPreset() then clears options->video_codec and
// video_bit_rate verbatim. (video_bit_rate == 0 alone does NOT mark a
// preset audio-only — in ExportOptions 0 already means "bridge
// default", so the codec string carries the flag. The engine currently
// derives stream presence from the project probe; honoring the
// audio-only request engine-side is a follow-up, same status as
// resolution_scale.)
//
// `resolution_scale` multiplies the project canvas (1.0 = native). It
// is written into ExportOptions::resolution_scale, which the engine
// does not consume yet — metadata only until compositor-side scaling
// lands.
struct ExportPreset {
    std::string name;
    std::string video_codec = "libx264";  // empty = audio-only preset
    int video_bit_rate = 8000000;         // bits/s; 0 = bridge default
    std::string audio_codec = "aac";
    int audio_bit_rate = 192000;          // bits/s; 0 = bridge default
    double resolution_scale = 1.0;        // (0, 1]; 1.0 = native canvas

    bool IsAudioOnly() const { return video_codec.empty(); }
};

// Factory presets ordered low -> high quality (the audio-only entry is
// appended last and does not participate in the bitrate ordering).
std::vector<ExportPreset> BuiltinPresets();

// Copies the preset's codec/bitrate/scale fields into `options` —
// verbatim, including the empty-video_codec audio-only flag.
// output_path and range_in/range_out are left untouched. A null
// `options` is a no-op.
void ApplyPreset(const ExportPreset& preset, ExportOptions* options);

// Per-user preset file location — deterministic, project-independent:
//   Windows : %LOCALAPPDATA%/XER/VideoEditor/export_presets.json
//   Linux   : $XDG_DATA_HOME/XER/VideoEditor/export_presets.json or
//             ~/.local/share/XER/VideoEditor/export_presets.json
//   fallback: <temp>/XER/VideoEditor/export_presets.json
// Chosen over "alongside the .xerproj" so presets follow the user
// across projects instead of scattering per-project files.
std::string DefaultPresetsPath();

// ---- JSON persistence (user presets) ---------------------------------
// File shape:
//   {"version":1,"presets":[{name, video_codec, video_bit_rate,
//                            audio_codec, audio_bit_rate,
//                            resolution_scale}, ...]}
// SavePresets writes compact JSON through a tmp+rename publish like
// ProjectStore::Save and creates the parent directory if needed.
// LoadPresets is tolerant: a MISSING file is not an error (returns
// true + empty list — first run), and malformed entries inside the
// array are skipped individually. It returns false only when the file
// exists but cannot be opened, is not parseable JSON, or has the wrong
// top-level shape (never throws).
bool SavePresets(const std::vector<ExportPreset>& presets,
                 const std::string& utf8_path,
                 std::string* error_out = nullptr);
bool LoadPresets(const std::string& utf8_path,
                 std::vector<ExportPreset>* presets_out,
                 std::string* error_out = nullptr);

}  // namespace XER::VideoEditor
