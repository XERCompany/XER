// audioplayer.exe — Audio player with folder playlist using XER AudioPlayer
//
// Usage: audioplayer.exe [--loop={track,playlist,none}] [--fade[=SEC]]
//                        [--fade-in=SEC] [--fade-out=SEC] <audio_file>
//
// On start the player scans the folder containing <audio_file> for supported
// audio files and plays them as a playlist (sorted by name). The opened file
// becomes the current track.
//
// Loop modes:
//   --loop=track     repeat the current track forever
//   --loop=playlist  repeat the whole folder playlist (default: none)
//   --loop=none      play the playlist once, then exit
//
// Fades (linear curve, default 5s in / 5s out):
//   --fade=SEC       set both fade-in and fade-out (--fade=0 disables)
//   --fade-in=SEC    fade-in only   --fade-out=SEC   fade-out only
//
// Controls:
//   Space      pause / resume
//   Left/Right seek -/+ 5 seconds
//   Up/Down    volume -/+ 5%
//   N          next track        P          previous track
//   M          mute toggle
//   Q          quit
//
// Displays a live progress line (bar, elapsed/total/remaining time, state,
// volume, track position) rendered through the Flux terminal.

#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <future>
#include <system_error>

#include <absl/strings/str_cat.h>

#include "app/app_audio_controller.h"
#include "async_io/io_thread_pool.h"
#include "audio/audio_core/audio_source_loader.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_styles.h"
#include "flux/terminal/terminal_extended.h"
#include "helper/string_utils.h"

#ifdef ENGINE_HAS_FFMPEG_BRIDGE
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#endif

#if defined(_WIN32)
#include <conio.h>
#define XER_PLAYER_HAS_CONSOLE_KEYS 1
#endif

namespace {
namespace Str = XER::Helper::String;
using XER::Flux::Terminal::OutputStream;
using XER::Flux::Terminal::TerminalColor;
using XER::Content::Audio::PlaybackState;

// Parses a non-negative fade duration; false on malformed/negative input.
bool ParseFadeSeconds(const std::string& text, double* out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || value < 0.0) {
        return false;
    }
    *out = value;
    return true;
}

void PrintUsage() {
    std::cout << "Usage: audioplayer.exe [--loop={track,playlist,none}] "
                 "[--fade[=SEC]] [--fade-in=SEC] [--fade-out=SEC] <audio_file>\n";
    std::cout << "\nScans the file's folder and plays all audio files as a playlist.\n";
    std::cout << "Loop: track=repeat current, playlist=repeat folder, none=play once (default)\n";
    std::cout << "\nControls: Space=pause/resume  Left/Right=seek 5s  Up/Down=volume  "
                 "N=next  P=prev  M=mute  Q=quit\n";
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
// state + volume + playlist position. Returns the string; caller wraps it
// in \r + clear-line.
std::string RenderStatusLine(double current, double duration, float volume,
                             PlaybackState state, const std::string& track_tag) {
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
    if (!track_tag.empty()) {
        line += "  ";
        line += ColorUtils::Dim("[" + track_tag + "]");
    }
    return line;
}

// ---------------------------------------------------------------------------
// Folder playlist
// ---------------------------------------------------------------------------

enum class LoopMode { kNone, kTrack, kPlaylist };

// Extensions the FFmpeg/WAV decode path can handle. Kept broad — files that
// fail to decode are skipped with an error message during playback.
constexpr std::string_view kAudioExtensions[] = {
    ".wav",  ".mp3",  ".flac", ".ogg",  ".oga",   ".opus", ".spx",
    ".m4a",  ".m4b",  ".aac",  ".wma",  ".aif",   ".aiff", ".ape",
    ".wv",   ".tta",  ".au",   ".snd",  ".caf",   ".mka",  ".ac3",
    ".eac3", ".mp2",  ".dts",  ".ra",   ".mp4",   ".mpc",  ".shn",
};

bool HasAudioExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& e : kAudioExtensions) {
        if (ext == e) return true;
    }
    return false;
}

std::string LowerFileName(const std::filesystem::path& p) {
    std::string s = p.filename().string();
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

struct FolderPlaylist {
    std::vector<std::string> files;
    std::size_t index = 0;
};

/// Scans the directory containing `input_path` for audio files, sorts them
/// by name and positions `index` on the opened file. Falls back to a
/// single-track playlist when the folder scan finds nothing usable.
FolderPlaylist ScanFolderPlaylist(const std::string& input_path) {
    FolderPlaylist playlist;
    std::error_code ec;
    const std::filesystem::path input =
        std::filesystem::weakly_canonical(std::filesystem::path(input_path), ec);
    const std::filesystem::path dir =
        ec ? std::filesystem::path(input_path).parent_path() : input.parent_path();

    for (const auto& entry :
         std::filesystem::directory_iterator(dir, ec)) {
        std::error_code entry_ec;
        if (entry.is_regular_file(entry_ec) &&
            HasAudioExtension(entry.path())) {
            playlist.files.push_back(
                entry.path().lexically_normal().string());
        }
        if (ec) break;
    }
    std::sort(playlist.files.begin(), playlist.files.end(),
              [](const std::string& a, const std::string& b) {
                  return LowerFileName(std::filesystem::path(a)) <
                         LowerFileName(std::filesystem::path(b));
              });

    if (!ec && !playlist.files.empty()) {
        for (std::size_t i = 0; i < playlist.files.size(); ++i) {
            std::error_code ec2;
            if (std::filesystem::equivalent(
                    playlist.files[i], input.string(), ec2)) {
                playlist.index = i;
                return playlist;
            }
        }
    }

    // Input missing from the scan (unsupported extension or vanished file):
    // keep it playable as a single-track playlist.
    playlist.files = {ec ? input_path
                         : input.lexically_normal().string()};
    playlist.index = 0;
    return playlist;
}

/// Advances the playlist cursor after a track finishes (or fails to load).
/// Manual next/previous always wrap; auto-advance honours the loop mode.
void AdvancePlaylist(FolderPlaylist& playlist, LoopMode loop,
                     bool manual, int direction, bool* playlist_done) {
    const std::size_t n = playlist.files.size();
    if (n <= 1) {
        *playlist_done = (loop == LoopMode::kNone);
        return;
    }
    if (manual) {
        const long long next =
            static_cast<long long>(playlist.index) + direction;
        playlist.index = static_cast<std::size_t>(
            (next + static_cast<long long>(n)) % static_cast<long long>(n));
        *playlist_done = false;
        return;
    }
    switch (loop) {
        case LoopMode::kTrack:
            *playlist_done = false;  // handled by the replay loop
            break;
        case LoopMode::kPlaylist:
            playlist.index = (playlist.index + 1) % n;
            *playlist_done = false;
            break;
        case LoopMode::kNone:
            if (playlist.index + 1 < n) {
                ++playlist.index;
                *playlist_done = false;
            } else {
                *playlist_done = true;
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Async decode-ahead (src/async_io thread pool)
// ---------------------------------------------------------------------------

struct DecodedTrack {
    XER::Audio::Core::AudioSourceBuffer buffer;
    std::string error;
    bool ok = false;
};

/// Decodes `path` on the shared IO thread pool with the same target format
/// the player was initialized with. The pool swallows task exceptions, so
/// the decode body is wrapped: the promise is always fulfilled.
std::future<DecodedTrack> DecodeTrackAsync(const std::string& path,
                                           int sample_rate, int channels) {
    auto promise = std::make_shared<std::promise<DecodedTrack>>();
    std::future<DecodedTrack> future = promise->get_future();
    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(
        [promise, path, sample_rate, channels]() mutable {
            DecodedTrack result;
            try {
                XER::Audio::Core::AudioSourceLoadOptions options;
                options.input_path = path;
                options.target_sample_rate = sample_rate;
                options.target_channels = channels;
                result.ok = XER::Audio::Core::AudioSourceLoader::Load(
                    options, &result.buffer, &result.error);
            } catch (const std::exception& e) {
                result.error = e.what();
            } catch (...) {
                result.error = "audio decode failed";
            }
            promise->set_value(std::move(result));
        });
    return future;
}

// Result of one playback session (one loaded track).
enum class SessionResult { kEnded, kNext, kPrev, kQuit };

/// Polls keys and redraws the status line until the track ends naturally,
/// the user requests a track switch, or quits.
SessionResult PlaySession(XER::App::AudioController& controller,
                          double duration, const std::string& track_tag) {
    bool quit = false;
    auto last_render = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    while (!quit) {
        const PlaybackState state = controller.GetState();
        if (state == PlaybackState::kStopped || state == PlaybackState::kError) {
            return SessionResult::kEnded;
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
                case 'n':
                case 'N':
                    return SessionResult::kNext;
                case 'p':
                case 'P':
                    return SessionResult::kPrev;
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
                                  controller.GetVolume(), controller.GetState(),
                                  track_tag)
            );
            XER::Flux::Terminal::Flush(OutputStream::kStdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return SessionResult::kQuit;
}

void PrintNowPlaying(std::size_t index, std::size_t total,
                     const std::string& path, double duration,
                     XER::Content::Audio::AudioPlayer* player) {
    char tag[32];
    std::snprintf(tag, sizeof(tag), "%zu/%zu", index + 1, total);
    XER::Flux::Terminal::WriteLine(
        OutputStream::kStdout,
        absl::StrCat(
            "\n",
            XER::Flux::Terminal::ForegroundSequence(TerminalColor::kGreen),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "Now playing [", tag, "] ", path,
            XER::Flux::Terminal::ResetStyleSequence(),
            "\n  Duration: ", FormatTime(duration),
            "   ", player->GetSampleRate(), " Hz / ",
            player->GetChannelCount(), " ch"
        )
    );
}

}  // namespace

int main(int argc, char** argv) {
    // Keep FFmpeg's banner/stream-info spam off the terminal; genuine
    // decode errors still surface through LoadFromFile's error string.
#ifdef ENGINE_HAS_FFMPEG_BRIDGE
    XER::Bridge::FFmpeg::SetLogLevel(XER::Bridge::FFmpeg::LogLevel::kError);
#endif

    // Parse --loop={track,playlist,none} (also accepts "--loop X") and the
    // fade options. -1 marks "not specified" so --fade-in/--fade-out can
    // override --fade regardless of argument order.
    LoopMode loop_mode = LoopMode::kNone;
    double fade_both = -1.0;
    double fade_in_opt = -1.0;
    double fade_out_opt = -1.0;
    std::string input_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;
        bool is_fade = false;
        double* fade_target = nullptr;
        if (arg.rfind("--loop=", 0) == 0) {
            value = arg.substr(7);
        } else if (arg == "--loop" && i + 1 < argc) {
            value = argv[++i];
        } else if (arg.rfind("--fade=", 0) == 0) {
            is_fade = true;
            fade_target = &fade_both;
            value = arg.substr(7);
        } else if (arg == "--fade") {
            fade_both = 5.0;  // bare flag: enable with the default length
        } else if (arg.rfind("--fade-in=", 0) == 0) {
            is_fade = true;
            fade_target = &fade_in_opt;
            value = arg.substr(10);
        } else if (arg == "--fade-in") {
            fade_in_opt = 5.0;
        } else if (arg.rfind("--fade-out=", 0) == 0) {
            is_fade = true;
            fade_target = &fade_out_opt;
            value = arg.substr(11);
        } else if (arg == "--fade-out") {
            fade_out_opt = 5.0;
        } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
            std::cerr << "Error: unknown option '" << arg << "'\n";
            PrintUsage();
            return 1;
        } else if (input_path.empty()) {
            input_path = arg;
        } else {
            std::cerr << "Error: unexpected argument '" << arg << "'\n";
            PrintUsage();
            return 1;
        }
        if (is_fade) {
            if (!ParseFadeSeconds(value, fade_target)) {
                std::cerr << "Error: invalid fade value '" << value << "'\n";
                return 1;
            }
        } else if (!value.empty()) {
            if (value == "track") {
                loop_mode = LoopMode::kTrack;
            } else if (value == "playlist") {
                loop_mode = LoopMode::kPlaylist;
            } else if (value == "none") {
                loop_mode = LoopMode::kNone;
            } else {
                std::cerr << "Error: invalid --loop value '" << value
                          << "' (expected track, playlist or none)\n";
                return 1;
            }
        }
    }
    if (input_path.empty()) {
        PrintUsage();
        return 1;
    }

    // Fade resolution: default 5s in / 5s out; --fade sets both sides,
    // --fade-in/--fade-out override their side regardless of order.
    const double fade_in = fade_in_opt >= 0.0 ? fade_in_opt
        : (fade_both >= 0.0 ? fade_both : 5.0);
    const double fade_out = fade_out_opt >= 0.0 ? fade_out_opt
        : (fade_both >= 0.0 ? fade_both : 5.0);

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
    controller.SetFadeInOut(fade_in, fade_out);

    // Scan the file's folder into a playlist.
    FolderPlaylist playlist = ScanFolderPlaylist(input_path);
    {
        char tag[64];
        std::snprintf(tag, sizeof(tag), "%zu tracks in %s",
                      playlist.files.size(),
                      std::filesystem::path(input_path).parent_path().string().c_str());
        XER::Flux::Terminal::WriteLine(
            OutputStream::kStdout,
            absl::StrCat(
                "Playlist: ", XER::Flux::Terminal::ColorUtils::Dim(tag)
            )
        );
    }

    bool quit = false;
    bool playlist_done = false;

    // Decode-ahead state: the next track in play order is decoded on the
    // shared async_io pool while the current one plays, so track changes
    // don't stall on the decoder.
    std::future<DecodedTrack> prefetch_future;
    std::string prefetch_path;
    const int device_rate = controller.GetPlayer()->GetSampleRate();
    const int device_channels = controller.GetPlayer()->GetChannelCount();

    // Index of the track that will play after the current one, or -1 when
    // playback ends here (loop=none at last track / loop=track replays in
    // place and needs no decode).
    auto auto_next_index = [&]() -> long long {
        const std::size_t n = playlist.files.size();
        if (loop_mode == LoopMode::kPlaylist) {
            return static_cast<long long>((playlist.index + 1) % n);
        }
        if (loop_mode == LoopMode::kNone && playlist.index + 1 < n) {
            return static_cast<long long>(playlist.index) + 1;
        }
        return -1;
    };
    auto request_prefetch = [&](long long index) {
        if (index < 0) {
            prefetch_path.clear();
            prefetch_future = {};
            return;
        }
        const std::string& path = playlist.files[static_cast<std::size_t>(index)];
        if (path == prefetch_path) return;  // already decoding
        prefetch_path = path;
        prefetch_future = DecodeTrackAsync(path, device_rate, device_channels);
    };

    // Hide the cursor while the status line is being redrawn.
    XER::Flux::Terminal::Screen::HideCursor();

    while (!quit && !playlist_done) {
        const std::string& track_path = playlist.files[playlist.index];
        char tag[32];
        std::snprintf(tag, sizeof(tag), "%zu/%zu",
                      playlist.index + 1, playlist.files.size());
        const std::string track_tag = tag;

        // Consume the prefetched decode when it matches this track;
        // otherwise fall back to a synchronous load.
        std::string error;
        bool loaded = false;
        if (!prefetch_path.empty() && prefetch_path == track_path) {
            DecodedTrack decoded = prefetch_future.get();
            prefetch_path.clear();
            prefetch_future = {};
            if (decoded.ok) {
                loaded = controller.LoadFromBuffer(std::move(decoded.buffer));
                if (!loaded) error = "failed to install decoded audio buffer";
            } else {
                error = decoded.error.empty() ? "decode failed" : decoded.error;
            }
        } else {
            loaded = controller.LoadFromFile(track_path, &error);
        }
        if (!loaded) {
            XER::Flux::Terminal::WriteLine(
                OutputStream::kStdout,
                absl::StrCat(
                    "\n",
                    XER::Flux::Terminal::ForegroundSequence(TerminalColor::kRed),
                    XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
                    "Cannot play [", track_tag, "] ", track_path,
                    ": ", error,
                    XER::Flux::Terminal::ResetStyleSequence()
                )
            );
            bool done = false;
            AdvancePlaylist(playlist, loop_mode, false, 1, &done);
            if (done) break;
            continue;
        }

        XER::Content::Audio::AudioPlayer* player = controller.GetPlayer();
        const double duration = controller.GetDuration();
        PrintNowPlaying(playlist.index, playlist.files.size(), track_path,
                        duration, player);
        if (!controller.Play()) {
            std::cerr << "Error: Failed to start playback\n";
            break;
        }

        // Start decoding the next track while this one plays.
        request_prefetch(auto_next_index());

        // Play the track; --loop=track replays it until skip/quit.
        SessionResult result;
        do {
            result = PlaySession(controller, duration, track_tag);
            if (result != SessionResult::kEnded || loop_mode != LoopMode::kTrack) {
                break;
            }
            // Natural end + track loop: audio thread already reset the
            // position to 0 and stopped the client — just press play again.
            // A failed restart falls through to the normal advance path.
            if (!controller.Play()) {
                break;
            }
        } while (!quit);

        switch (result) {
            case SessionResult::kQuit:
                quit = true;
                break;
            case SessionResult::kNext:
            case SessionResult::kPrev: {
                bool done = false;
                AdvancePlaylist(playlist, loop_mode,
                                true,
                                result == SessionResult::kNext ? 1 : -1,
                                &done);
                break;
            }
            case SessionResult::kEnded: {
                bool done = false;
                AdvancePlaylist(playlist, loop_mode, false, 1, &done);
                if (done) playlist_done = true;  // playlist exhausted
                break;
            }
        }
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
            "Playlist finished",
            XER::Flux::Terminal::ResetStyleSequence(),
            "  ", playlist.files.size(), " track(s)"
        )
    );

    return 0;
}
