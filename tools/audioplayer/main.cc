// audioplayer.exe — Simple audio player using XER AudioPlayer
//
// Usage: audioplayer.exe <audio_file>
//
// Plays audio files with basic controls:
//   Space      pause / resume
//   Left/Right seek -/+ 5 seconds
//   Up/Down    volume -/+ 5%
//   M          mute toggle
//   Q          quit
//
// Displays a live progress line (bar, elapsed/total/remaining time, state,
// volume) rendered through the Flux terminal.

#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <filesystem>
#include <algorithm>

#include <absl/strings/str_cat.h>

#include "app/app_audio_controller.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_styles.h"
#include "flux/terminal/terminal_extended.h"
#include "helper/string_utils.h"

#if defined(_WIN32)
#include <conio.h>
#define XER_PLAYER_HAS_CONSOLE_KEYS 1
#endif

namespace {
namespace Str = XER::Helper::String;
using XER::Flux::Terminal::OutputStream;
using XER::Flux::Terminal::TerminalColor;
using XER::Content::Audio::PlaybackState;

void PrintUsage() {
    std::cout << "Usage: audioplayer.exe <audio_file>\n";
    std::cout << "\nControls: Space=pause/resume  Left/Right=seek 5s  Up/Down=volume  M=mute  Q=quit\n";
}

std::string FormatTime(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds);
    const int tenths = static_cast<int>((seconds - total) * 10.0);
    const int hours = total / 3600;
    const int mins = (total % 3600) / 60;
    const int secs = total % 60;
    char buf[24];
    if (hours > 0) {
        snprintf(buf, sizeof(buf), "%d:%02d:%02d.%d", hours, mins, secs, tenths);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d.%d", mins, secs, tenths);
    }
    return std::string(buf);
}

const char* StateName(PlaybackState state) {
    switch (state) {
        case PlaybackState::kPlaying: return "Playing";
        case PlaybackState::kPaused:  return "Paused ";
        case PlaybackState::kStopped: return "Stopped";
        case PlaybackState::kError:   return "Error ";
    }
    return "?";
}

TerminalColor StateColor(PlaybackState state) {
    switch (state) {
        case PlaybackState::kPlaying: return TerminalColor::kGreen;
        case PlaybackState::kPaused:  return TerminalColor::kYellow;
        case PlaybackState::kError:   return TerminalColor::kRed;
        default:                      return TerminalColor::kBrightBlack;
    }
}

#if XER_PLAYER_HAS_CONSOLE_KEYS
// Poll the console for a key without blocking. Arrow keys arrive as a
// two-byte sequence (0x00/0xE0 prefix + scan code) and are normalized to
// 'L'/'R'/'U'/'D'; plain keys pass through lowercased.
bool TryReadKey(char* out) {
    if (_kbhit() == 0) {
        return false;
    }
    const int first = _getch();
    if (first == 0 || first == 0xE0) {
        const int second = _getch();
        switch (second) {
            case 75: *out = 'L'; return true;  // Left
            case 77: *out = 'R'; return true;  // Right
            case 72: *out = 'U'; return true;  // Up
            case 80: *out = 'D'; return true;  // Down
            default:  return TryReadKey(out); // ignore other extended keys
        }
    }
    *out = static_cast<char>(first);
    return true;
}
#endif

// One status line: progress bar + percentage + elapsed/total/remaining +
// state + volume. Returns the string; caller wraps it in \r + clear-line.
std::string RenderStatusLine(double current, double duration, float volume,
                             PlaybackState state) {
    using namespace XER::Flux::Terminal;

    const double fraction = duration > 0.0
        ? std::clamp(current / duration, 0.0, 1.0)
        : 0.0;

    ProgressBarStyle bar_style;
    bar_style.width = 30;
    bar_style.fill_char = '#';
    bar_style.empty_char = '-';
    bar_style.bar_color = TerminalColor::kCyan;
    bar_style.show_label = false;
    bar_style.show_percentage = false;
    ProgressBar bar("", 1000, bar_style);
    bar.Update(static_cast<int>(fraction * 1000.0));

    const int percent = static_cast<int>(fraction * 100.0 + 0.5);
    const double remaining = std::max(0.0, duration - current);

    std::string line;
    line += "\r";
    line += "\x1b[2K";  // clear entire line before redraw
    line += bar.Render();
    line += " ";
    line += ColorUtils::ApplyStyle(std::to_string(percent) + "%", TextStyle::kBold);
    line += "  ";
    line += ColorUtils::Cyan(FormatTime(current));
    line += "/";
    line += ColorUtils::BrightWhite(FormatTime(duration));
    line += "  (-";
    line += ColorUtils::Dim(FormatTime(remaining));
    line += ")  ";
    line += ColorUtils::ApplyColor(StateName(state), StateColor(state), TerminalColor::kDefault);
    line += "  Vol ";
    line += std::to_string(static_cast<int>(volume * 100.0f + 0.5f)) + "%";
    return line;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string input_path = argv[1];

    XER::Flux::Terminal::WriteLine(
        OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(TerminalColor::kCyan),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "XER Audio Player",
            XER::Flux::Terminal::ResetStyleSequence()
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
    XER::Content::Audio::AudioPlayer* player = controller.GetPlayer();

    XER::Flux::Terminal::WriteLine(
        OutputStream::kStdout,
        absl::StrCat(
            "Now playing: ", XER::Flux::Terminal::ColorUtils::Cyan(input_path), "\n",
            "Duration: ", FormatTime(duration),
            "   ", player->GetSampleRate(), " Hz / ", player->GetChannelCount(), " ch"
        )
    );

    // Start playback
    if (!controller.Play()) {
        std::cerr << "Error: Failed to start playback\n";
        return 1;
    }

    // Live status line — hide the cursor while it is being redrawn.
    XER::Flux::Terminal::Screen::HideCursor();

    bool quit = false;
    // Back-date the clock so the first status line renders immediately;
    // time_point::min() would overflow the duration subtraction below.
    auto last_render = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    while (!quit) {
        const PlaybackState state = controller.GetState();
        if (state == PlaybackState::kStopped || state == PlaybackState::kError) {
            break;
        }

#if XER_PLAYER_HAS_CONSOLE_KEYS
        char key = 0;
        while (TryReadKey(&key)) {
            switch (key) {
                case ' ':
                    if (state == PlaybackState::kPlaying) {
                        controller.Pause();
                    } else {
                        controller.Play();
                    }
                    break;
                case 'q':
                case 'Q':
                    quit = true;
                    break;
                case 'L':
                    controller.Seek(controller.GetCurrentTime() - 5.0);
                    break;
                case 'R':
                    controller.Seek(controller.GetCurrentTime() + 5.0);
                    break;
                case 'U':
                    controller.SetVolume(std::min(1.0f, controller.GetVolume() + 0.05f));
                    break;
                case 'D':
                    controller.SetVolume(std::max(0.0f, controller.GetVolume() - 0.05f));
                    break;
                case 'm':
                case 'M':
                    controller.Mute(!controller.IsMuted());
                    break;
                default:
                    break;
            }
            if (quit) {
                break;
            }
        }
#else
        (void)0;
#endif

        const auto now = std::chrono::steady_clock::now();
        if (now - last_render >= std::chrono::milliseconds(100)) {
            last_render = now;
            XER::Flux::Terminal::Write(
                OutputStream::kStdout,
                RenderStatusLine(controller.GetCurrentTime(), duration,
                                  controller.GetVolume(), controller.GetState())
            );
            XER::Flux::Terminal::Flush(OutputStream::kStdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    XER::Flux::Terminal::Screen::ShowCursor();

    if (quit) {
        controller.Stop();
        XER::Flux::Terminal::WriteLine(
            OutputStream::kStdout,
            absl::StrCat(
                "\n",
                XER::Flux::Terminal::ForegroundSequence(TerminalColor::kYellow),
                XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
                "Playback stopped",
                XER::Flux::Terminal::ResetStyleSequence()
            )
        );
        return 0;
    }

    XER::Flux::Terminal::WriteLine(
        OutputStream::kStdout,
        absl::StrCat(
            "\n",
            XER::Flux::Terminal::ForegroundSequence(TerminalColor::kGreen),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "Playback finished",
            XER::Flux::Terminal::ResetStyleSequence(),
            "  ", FormatTime(duration)
        )
    );

    return 0;
}
