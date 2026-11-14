// audioplayer.exe — Simple audio player using XER AudioPlayer
//
// Usage: audioplayer.exe <audio_file>
//
// Plays audio files with basic controls: space to pause/resume, q to quit.
// Displays playback position and duration.

#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <filesystem>

#include <absl/strings/str_cat.h>

#include "app/app_audio_controller.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_styles.h"
#include "helper/string_utils.h"

namespace {
namespace Str = XER::Helper::String;

void PrintUsage() {
    std::cout << "Usage: audioplayer.exe <audio_file>\n";
    std::cout << "\nPlays audio file until completion.\n";
}

std::string FormatTime(double seconds) {
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    return std::string(buf);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string input_path = argv[1];

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kCyan),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "XER Audio Player"
        )
    );

    // Initialize audio controller
    XER::App::AudioController controller;
    if (!controller.Initialize(44100, 2)) {
        std::cerr << "Error: Failed to initialize audio controller\n";
        return 1;
    }

    // Load audio file
    std::string error;
    if (!controller.LoadFromFile(input_path, &error)) {
        std::cerr << "Error loading audio: " << error << "\n";
        return 1;
    }

    const double duration = controller.GetDuration();

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Playing: ", input_path, "\n",
            "Duration: ", FormatTime(duration)
        )
    );

    // Start playback
    if (!controller.Play()) {
        std::cerr << "Error: Failed to start playback\n";
        return 1;
    }

    // Wait for playback to complete
    while (controller.GetState() != XER::Content::Audio::PlaybackState::kStopped) {
        const double current_time = controller.GetCurrentTime();

        XER::Flux::Terminal::ClearSnapshot(XER::Flux::Terminal::OutputStream::kStdout);
        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat(
                "\r",
                FormatTime(current_time), " / ", FormatTime(duration),
                std::string(10, ' ')
            )
        );

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "\n",
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kGreen),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "Playback finished"
        )
    );

    return 0;
}
