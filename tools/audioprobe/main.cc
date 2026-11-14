// audioprobe.exe — Audio file information probe
//
// Usage: audioprobe.exe <audio_file>
//
// Displays detailed information about audio files: format, codec, sample rate,
// channels, duration, bit depth, and metadata.

#include <iostream>
#include <string>
#include <cmath>
#include <vector>
#include <filesystem>

#include <absl/strings/str_cat.h>

#include "audio/audio_core/audio_source_loader.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_styles.h"
#include "helper/string_utils.h"

namespace {
namespace AudioCore = XER::Audio::Core;
namespace Str = XER::Helper::String;

void PrintUsage() {
    std::cout << "Usage: audioprobe.exe <audio_file>\n";
    std::cout << "\nDisplays detailed information about audio files.\n";
}

std::string FormatDuration(double seconds) {
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d.%03d", mins, secs, ms);
    return std::string(buf);
}

double CalculateRMS(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0;
    double sum = 0.0;
    for (float sample : samples) {
        sum += static_cast<double>(sample) * sample;
    }
    return std::sqrt(sum / samples.size());
}

double CalculatePeak(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0;
    float peak = 0.0f;
    for (float sample : samples) {
        const float abs_sample = std::abs(sample);
        if (abs_sample > peak) peak = abs_sample;
    }
    return static_cast<double>(peak);
}

double ToDb(double linear) {
    return 20.0 * std::log10(linear + 1e-10);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string input_path = argv[1];

    // Load audio info (load minimal data)
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
            "Audio File Probe: ", input_path,
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    if (!AudioCore::AudioSourceLoader::Load(options, &buffer, &error)) {
        std::cerr << "Error loading audio: " << error << "\n";
        return 1;
    }

    const double duration = static_cast<double>(buffer.frame_count) / buffer.sample_rate;
    const double peak = CalculatePeak(buffer.samples);
    const double rms = CalculateRMS(buffer.samples);

    // Print file information
    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Format Information ===",
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Source Format: ", buffer.source_format, "\n",
            "Codec: ", buffer.codec_name, "\n",
            "Decode Backend: ", buffer.decode_backend
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Audio Properties ===",
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Sample Rate: ", buffer.sample_rate, " Hz\n",
            "Original Sample Rate: ", buffer.original_sample_rate, " Hz\n",
            "Channels: ", buffer.channels, "\n",
            "Original Channels: ", buffer.original_channels, "\n",
            "Frames: ", buffer.frame_count, "\n",
            "Duration: ", FormatDuration(duration), " (", duration, " s)"
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Audio Analysis ===",
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Peak Level: ", peak, " (", ToDb(peak), " dBFS)\n",
            "RMS Level: ", rms, " (", ToDb(rms), " dBFS)\n",
            "Dynamic Range: ", ToDb(peak / (rms + 1e-10)), " dB"
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Buffer Information ===",
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Sample Count: ", buffer.samples.size(), "\n",
            "Original Frames: ", buffer.original_frame_count
        )
    );

    return 0;
}
