// audioconvert.exe — Audio format converter using XER audio codecs
//
// Usage: audioconvert.exe <input> <output> [--rate <sample_rate>] [--channels <n>]
//
// Converts audio files between formats (wav, mp3, flac, ogg, opus, m4a, aac)
// using XER's FFmpeg and built-in codec helpers.

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <charconv>
#include <filesystem>

#include <absl/strings/str_cat.h>

#include "audio/audio_core/audio_source_loader.h"
#include "audio/file_io_codecs/codec_ffmpeg_encode_helper.h"
#include "audio/file_io_codecs/codec_file_streamer.h"
#include "audio/file_io_codecs/codec_mp3_lame.h"
#include "audio/file_io_codecs/codec_wav_float.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_styles.h"
#include "helper/string_utils.h"

namespace {
namespace AudioCore = XER::Audio::Core;
namespace Codec = XER::Audio::CodecIO;
namespace Str = XER::Helper::String;

void PrintUsage() {
    std::cout << "Usage: audioconvert.exe <input> <output> [--rate <sample_rate>] [--channels <n>]\n";
    std::cout << "\nSupported formats: wav, mp3, flac, ogg, opus, m4a, aac\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --rate <n>         Target sample rate (default: keep original)\n";
    std::cout << "  --channels <n>     Target channel count (default: keep original)\n";
}

bool EncodeForPath(const std::vector<float>& samples, int sample_rate, int channels,
                   const std::string& path, std::vector<uint8_t>* bytes, std::string* error) {
    const std::string ext = Str::ToLowerAscii(path.substr(path.find_last_of('.')));
    const size_t frames = samples.size() / static_cast<size_t>(channels);
    const float* data = samples.data();

    if (ext == ".wav") {
        if (Codec::WavPcmCodec().Encode16(data, frames, *bytes, sample_rate, channels)) {
            return true;
        }
        *error = "wav encode failed";
        return false;
    }
    if (ext == ".mp3") {
        if (Codec::Mp3LameCodec().EncodeInterleaved(data, frames, sample_rate, channels, *bytes)) {
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
    return Codec::detail::EncodeInterleavedAudioBufferWithFfmpeg(
        data, frames, sample_rate, channels, ext, codec->second, bytes, error);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        PrintUsage();
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = argv[2];
    int target_rate = 0;
    int target_channels = 0;

    for (int i = 3; i < argc; i += 2) {
        const std::string flag = argv[i];
        if (i + 1 >= argc) {
            std::cerr << "Error: flag " << flag << " requires a value\n";
            PrintUsage();
            return 1;
        }
        if (flag == "--rate") {
            target_rate = std::atoi(argv[i + 1]);
        } else if (flag == "--channels") {
            target_channels = std::atoi(argv[i + 1]);
        } else {
            std::cerr << "Error: unknown flag " << flag << "\n";
            PrintUsage();
            return 1;
        }
    }

    // Load input audio
    AudioCore::AudioSourceLoadOptions options;
    options.input_path = std::filesystem::path(input_path);
    options.target_sample_rate = target_rate > 0 ? target_rate : 44100;
    options.target_channels = target_channels > 0 ? target_channels : 0;
    options.resample_quality = AudioCore::ResampleQuality::HIGH;

    AudioCore::AudioSourceBuffer buffer;
    std::string error;

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kCyan),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "Loading audio: ", input_path
        )
    );

    if (!AudioCore::AudioSourceLoader::Load(options, &buffer, &error)) {
        std::cerr << "Error loading audio: " << error << "\n";
        return 1;
    }

    // Keep original sample rate if not specified
    if (target_rate == 0 && buffer.original_sample_rate > 0) {
        if (buffer.original_sample_rate != buffer.sample_rate) {
            options.target_sample_rate = buffer.original_sample_rate;
            buffer = {};
            if (!AudioCore::AudioSourceLoader::Load(options, &buffer, &error)) {
                std::cerr << "Error reloading audio with original rate: " << error << "\n";
                return 1;
            }
        }
    }

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Sample rate: ", buffer.sample_rate, " Hz\n",
            "Channels: ", buffer.channels, "\n",
            "Frames: ", buffer.frame_count, "\n",
            "Duration: ", static_cast<double>(buffer.frame_count) / buffer.sample_rate, " s\n",
            "Format: ", buffer.source_format
        )
    );

    // Encode output
    std::vector<uint8_t> encoded_bytes;
    if (!EncodeForPath(buffer.samples, buffer.sample_rate, buffer.channels,
                       output_path, &encoded_bytes, &error)) {
        std::cerr << "Error encoding audio: " << error << "\n";
        return 1;
    }

    // Write output file
    Codec::CodecFileStreamer file;
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
            "Successfully converted to: ", output_path, " (", encoded_bytes.size(), " bytes)"
        )
    );

    return 0;
}
