// audioapplyeffect.exe — Apply audio effects to input file and save to output
//
// Usage: audioapplyeffect.exe <input> <output> <effect> [node.param=value ...]
//
// Applies XER audio effects (compressor, reverb, chorus, eq, etc.) to an audio file.
// Run with --list to see available effects.

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <charconv>
#include <filesystem>

#include <absl/strings/str_cat.h>

#include "audio/audio_core/audio_source_loader.h"
#include "audio/effects_rack/custom_effect.h"
#include "audio/effects_rack/custom_effect_invoke.h"
#include "audio/effects_rack/custom_effect_package.h"
#include "audio/file_io_codecs/codec_ffmpeg_encode_helper.h"
#include "audio/file_io_codecs/codec_file_streamer.h"
#include "audio/file_io_codecs/codec_mp3_lame.h"
#include "audio/file_io_codecs/codec_wav_float.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "audio/fx_customs/fx_customs_presets.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_styles.h"
#include "helper/string_utils.h" 

namespace {
namespace AudioCore = XER::Audio::Core;
namespace FX = XER::Audio::FX;
namespace Str = XER::Helper::String;

void PrintUsage() {
    std::cout << "Usage: audioapplyeffect.exe <input> <output> <effect[;effect...]> [node.param=value ...]\n";
    std::cout << "   or: audioapplyeffect.exe --list\n";
    std::cout << "\nEffects are applied sequentially, in the order given.\n";
    std::cout << "Parameters apply to every effect that has the node; prefix with\n";
    std::cout << "an effect name to target one: <effect>.<node>.<param>=<value>.\n";
    std::cout << "\nExamples:\n";
    std::cout << "  audioapplyeffect.exe input.wav output.wav compressor\n";
    std::cout << "  audioapplyeffect.exe input.wav output.wav \"RoomReverb;BassBoost\"\n";
    std::cout << "  audioapplyeffect.exe input.wav output.wav reverb reverb.room_size=0.8\n";
    std::cout << "  audioapplyeffect.exe input.wav output.wav \"Reverb;Compressor\" Compressor.threshold=-12\n";
    std::cout << "  audioapplyeffect.exe input.wav output.wav eq parametric_eq.freq=1000\n";
}

void PrintEffectList() {
    const auto effects = FX::Customs::ListFxCustomPresetNames();
    std::cout << "Available effects:\n";
    for (const auto& effect : effects) {
        std::cout << "  - " << effect << "\n";
    }
}

bool EncodeForPath(const std::vector<float>& samples, int sample_rate, int channels,
                   const std::string& path, std::vector<uint8_t>* bytes, std::string* error) {
    const size_t dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos
        ? std::string()
        : Str::ToLowerAscii(path.substr(dot));
    const size_t frames = samples.size() / static_cast<size_t>(channels);
    const float* data = samples.data();

    if (ext == ".wav") {
        if (XER::Audio::CodecIO::WavPcmCodec().Encode16(data, frames, *bytes, sample_rate, channels)) {
            return true;
        }
        *error = "wav encode failed";
        return false;
    }
    if (ext == ".mp3") {
        if (XER::Audio::CodecIO::Mp3LameCodec().EncodeInterleaved(data, frames, sample_rate, channels, *bytes)) {
            return true;
        }
        *error = "mp3 encode failed";
        return false;
    }

    static const std::map<std::string, std::string> kFfmpegCodecs = {
        {".flac", "flac"}, {".ogg", "libvorbis"}, {".opus", "libopus"},
        {".m4a", "aac"},   {".aac", "aac"}
    };
    const auto codec = kFfmpegCodecs.find(ext);
    if (codec == kFfmpegCodecs.end()) {
        *error = "unsupported output format '" + ext +
                 "' (wav, mp3, flac, ogg, opus, m4a, aac)";
        return false;
    }
    return XER::Audio::CodecIO::detail::EncodeInterleavedAudioBufferWithFfmpeg(
        data, frames, sample_rate, channels, ext, codec->second, bytes, error);
}

template <typename T>
bool ParseNumber(std::string_view text, T* out) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), *out);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const std::string first_arg = argv[1];
    if (first_arg == "--list" || first_arg == "-l") {
        PrintEffectList();
        return 0;
    }

    if (argc < 4) {
        PrintUsage();
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = argv[2];
    const std::string effect_arg = argv[3];

    // Effect chain: "Name1;Name2;..." — each segment is one preset.
    std::vector<std::string> effect_names;
    for (const std::string& part : Str::Split(effect_arg, ';')) {
        const std::string name = Str::TrimAsciiWhitespaceCopy(part);
        if (!name.empty()) effect_names.push_back(name);
    }
    if (effect_names.empty()) {
        std::cerr << "Error: empty effect list\n";
        return 1;
    }
    for (const std::string& name : effect_names) {
        if (!FX::Customs::IsFxCustomPresetSupported(name)) {
            std::cerr << "Error: unknown effect '" << name << "'\n";
            std::cerr << "Run --list to see available effects\n";
            return 1;
        }
    }

    // Load input audio
    AudioCore::AudioSourceLoadOptions options;
    options.input_path = std::filesystem::path(input_path);
    options.target_channels = 0;
    options.resample_quality = AudioCore::ResampleQuality::HIGH;

    AudioCore::AudioSourceBuffer buffer;
    std::string error;

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kCyan),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "Loading audio: ", input_path,
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    if (!AudioCore::AudioSourceLoader::Load(options, &buffer, &error)) {
        std::cerr << "Error loading audio: " << error << "\n";
        return 1;
    }

    // Keep original sample rate
    if (buffer.original_sample_rate > 0 && buffer.original_sample_rate != buffer.sample_rate) {
        options.target_sample_rate = buffer.original_sample_rate;
        buffer = {};
        if (!AudioCore::AudioSourceLoader::Load(options, &buffer, &error)) {
            std::cerr << "Error reloading audio with original rate: " << error << "\n";
            return 1;
        }
    }

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Sample rate: ", buffer.sample_rate, " Hz\n",
            "Channels: ", buffer.channels, "\n",
            "Frames: ", buffer.frame_count
        )
    );

    // Build one package per effect in the chain.
    std::vector<FX::CustomEffectPackage> packages;
    packages.reserve(effect_names.size());
    for (const std::string& name : effect_names) {
        packages.push_back(FX::Customs::BuildFxCustomPreset(name));
    }

    // Parse additional parameters.
    //   node.param=value            -> applied to every effect that has `node`
    //   <effect>.<node>.<param>=v    -> applied only to the named effect
    for (int i = 4; i < argc; ++i) {
        const std::string param = argv[i];
        const std::vector<std::string> kv = Str::Split(param, '=');
        const size_t last_dot = kv.empty() ? std::string::npos : kv[0].rfind('.');
        const size_t first_dot = kv.empty() ? std::string::npos : kv[0].find('.');

        float value = 0.0f;
        if (kv.size() != 2 || last_dot == std::string::npos ||
            first_dot == last_dot ||
            !ParseNumber(std::string_view(kv[1]), &value)) {
            std::cerr << "Error: bad parameter '" << param << "' (expected node.param=value)\n";
            return 1;
        }

        const std::string param_name = kv[0].substr(last_dot + 1);
        std::vector<size_t> targets;
        std::string node_name;
        std::string scoped_effect;

        if (first_dot != std::string::npos && first_dot < last_dot) {
            // "<effect>.<node>.<param>" — scoped to one effect.
            scoped_effect = kv[0].substr(0, first_dot);
            node_name = kv[0].substr(first_dot + 1, last_dot - first_dot - 1);
            for (size_t e = 0; e < effect_names.size(); ++e) {
                if (Str::EqualsIgnoreAsciiCase(effect_names[e], scoped_effect)) {
                    targets.push_back(e);
                    break;
                }
            }
            if (targets.empty()) {
                std::cerr << "Error: parameter '" << param
                          << "' names unknown effect '" << scoped_effect << "'\n";
                return 1;
            }
        } else {
            // "node.param" — broadcast to all effects.
            node_name = kv[0].substr(0, last_dot);
            targets.resize(packages.size());
            for (size_t e = 0; e < packages.size(); ++e) targets[e] = e;
        }

        int applied = 0;
        for (const size_t e : targets) {
            if (FX::InvokeCustomEffectParameter(&packages[e], node_name,
                                                param_name, value)) {
                ++applied;
            }
        }
        if (applied == 0) {
            std::cerr << "Error: no effect in the chain has node '" << node_name << "'\n";
            return 1;
        }

        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat("Set parameter: ", param,
                         " (applied to ", applied, " effect(s))")
        );
    }

    // Apply the chain sequentially: output of each stage feeds the next.
    std::vector<float> output = buffer.samples;
    for (size_t i = 0; i < packages.size(); ++i) {
        const std::string& name = effect_names[i];
        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat(
                XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
                XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
                "Applying effect ", i + 1, "/", packages.size(), ": ", name, "...",
                XER::Flux::Terminal::ResetStyleSequence()
            )
        );

        std::vector<float> step_out;
        FX::CustomEffectReport report;
        if (!FX::RunCustomEffectPackageInterleaved(
                packages[i], static_cast<float>(buffer.sample_rate), output,
                buffer.channels, &step_out, &report, &error)) {
            std::cerr << "Error applying effect '" << name << "': " << error << "\n";
            return 1;
        }

        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat(
                "  Peak: ", report.peak, "  RMS: ", report.rms
            )
        );
        output = std::move(step_out);
    }

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kGreen),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "All ", packages.size(), " effect(s) applied",
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    // Encode output
    std::vector<uint8_t> encoded_bytes;
    if (!EncodeForPath(output, buffer.sample_rate, buffer.channels,
                       output_path, &encoded_bytes, &error)) {
        std::cerr << "Error encoding audio: " << error << "\n";
        return 1;
    }

    // Write output file
    XER::Audio::CodecIO::CodecFileStreamer file;
    if (!file.OpenWrite(output_path)) {
        std::cerr << "Error: cannot open output file " << output_path << "\n";
        return 1;
    }

    const size_t written = file.WriteBytes(encoded_bytes.data(), encoded_bytes.size());
    file.Close();

    if (written != encoded_bytes.size()) {
        std::cerr << "Error: short write to output file\n";
        return 1;
    }

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kGreen),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "Successfully saved to: ", output_path, " (", encoded_bytes.size(), " bytes)",
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    return 0;
}
