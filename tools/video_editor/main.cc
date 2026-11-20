// video_editor.exe — XER Studio Phase 1C/1D executable.
//   video_editor [project.xerproj]                  Premiere-style GUI
//   video_editor --open <in.xerproj>                open project in GUI
//   video_editor --export <in.xerproj> <out.mp4>   headless render+encode
//   video_editor --info <in.xerproj>               project summary
//   video_editor --smoke                           headless self-checks
//   video_editor --smoke-list                      list check names
//   video_editor --help

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "content/video/video_info.h"
#include "export_engine.h"
#include "media_worker_server.h"
#include "helper/path_utils.h"
#include "playback/audio_output.h"
#include "playback/audio_sink_factory.h"
#include "playback/preview_engine.h"
#include "project_store.h"
#include "smoke_checks.h"
#include "timeline_model.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/theme/theme.h"
#include "ui/ve_theme.h"
#include "ui/video_editor_app.h"
#include "ui/widgets/app_title_bar.h"

#include "app_runtime/app_runtime.h"
#include "app_runtime/js_engine.h"  // complete type for BootResult dtor
#include "cef/cef_host.h"           // --cef-check browser bring-up
#include "cef/cef_runtime.h"        // CEF subprocess early-out

#include <atomic>
#include <cmath>
#include <thread>
#endif

namespace {
namespace Path = XER::Helper::Path;
using XER::VideoEditor::ExportEngine;
using XER::VideoEditor::ExportOptions;
using XER::VideoEditor::ExportProgress;
using XER::VideoEditor::ExportStatus;
using XER::VideoEditor::Project;
using XER::VideoEditor::SourceKind;
using XER::VideoEditor::TrackKind;

#if defined(_WIN32)
#if defined(_MSC_VER)
#pragma comment(lib, "shell32.lib")
#endif

std::string WideArgToUtf8(const wchar_t* w) {
    if (w == nullptr || *w == L'\0') return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.pop_back();  // drop the trailing NUL written by -1 sizing
    return s;
}

// argv from char** main() is ANSI-encoded and mangles non-ASCII paths.
// Re-parse the original UTF-16 command line so every argument arrives
// as UTF-8 (same pattern as tools/preview/main.cc).
std::vector<std::string> CommandLineArgsUtf8() {
    std::vector<std::string> out;
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv == nullptr) return out;
    out.reserve(static_cast<size_t>(wargc));
    for (int i = 0; i < wargc; ++i) {
        out.push_back(WideArgToUtf8(wargv[i]));
    }
    LocalFree(wargv);
    return out;
}
#endif

void PrintUsage() {
    std::cout << "Usage:\n"
              << "  video_editor [project.xerproj]                 "
                 "Open the desktop editor\n"
              << "  video_editor --open <in.xerproj>               "
                 "Open project in the desktop editor\n"
              << "  video_editor --export <in.xerproj> <out.mp4>  "
                 "Render and encode a project\n"
              << "  video_editor --info <in.xerproj>              "
                 "Print project/media summary\n"
              << "  video_editor --smoke                          "
                 "Run headless self-checks (SMOKE <name> OK/FAIL)\n"
              << "  video_editor --cef-check                      "
                 "Headless CEF bring-up (runtime init + browser)\n"
              << "  video_editor --smoke-list                     "
                 "List the smoke check names\n"
              << "  video_editor --help                           "
                 "Show this help\n";
}

#if defined(_WIN32)
int RunGui(const std::string& project_path) {
    // Declarative assets layer: assets/app.yaml → runtime.js (real V8)
    // → window/window.yaml. Missing assets boot with compiled defaults.
    std::string boot_status;
    auto boot = XER::VideoEditor::AppRuntime::Boot(&boot_status);
    if (boot.ok) {
        std::cerr << "assets: " << boot_status << "\n";
    } else if (!boot_status.empty()) {
        std::cerr << "assets: " << boot_status << "\n";
    }
    const auto& wcfg = boot.window;

    auto& app = XER::UI::Core::Application::Instance();
    app.SetAppName("XER Video Editor");
    if (!app.Initialize()) {
        std::cerr << "Error: failed to initialize XER UI\n";
        return 1;
    }

    auto window = std::make_shared<XER::UI::Core::Window>();
    if (!window->Create(wcfg.title, wcfg.width, wcfg.height)) {
        app.Shutdown();
        std::cerr << "Error: failed to create editor window\n";
        return 1;
    }
    if (wcfg.maximized) {
        window->ToggleMaximize();
    }
    auto editor = std::make_shared<XER::VideoEditor::Ui::VideoEditorAppWidget>(
        window->GetHWND(), project_path);
    window->SetContent(editor);
    window->SetTheme(XER::UI::Theme::Variant::Dark);
    // Editor design tokens (ui/ve_theme.h): refined dark palette on top
    // of the builtin Dark variant; assets/editor.yaml `Accent:` wins.
    XER::VideoEditor::Ui::VeTheme::ApplyToWindow(
        window.get(),
        XER::VideoEditor::AppRuntime::SharedAssetsConfig()
            .editor.accent_argb);
    window->SetCustomTitleBar(true, 38.0f);
    window->SetTitleBarHitTest([editor](XER::UI::Layout::Point point) {
        auto* title_bar = editor->TitleBar();
        return title_bar != nullptr && title_bar->IsInteractiveAt(point);
    });
    auto* title_bar = editor->TitleBar();
    if (title_bar != nullptr) {
        using WindowCommand =
            XER::UI::Widgets::AppTitleBar::WindowCommand;
        title_bar->SetOnWindowCommand([window](WindowCommand command) {
            switch (command) {
                case WindowCommand::Minimize:
                    window->Minimize();
                    break;
                case WindowCommand::ToggleMaximize:
                    window->ToggleMaximize();
                    break;
                case WindowCommand::Close:
                    window->Close();
                    break;
            }
        });
        title_bar->SetThemeToggle(true, [window](bool dark) {
            window->SetTheme(dark ? XER::UI::Theme::Variant::Dark
                                  : XER::UI::Theme::Variant::Light);
            if (dark) {
                // SetTheme() restores the stock variant — re-layer the
                // editor palette so toggling back keeps the VE look.
                XER::VideoEditor::Ui::VeTheme::ApplyToWindow(
                    window.get(), XER::VideoEditor::AppRuntime::
                                      SharedAssetsConfig()
                                          .editor.accent_argb);
            }
        });
    }
    const auto sync_maximize = [window, title_bar] {
        if (title_bar != nullptr) {
            title_bar->SetMaximized(window->IsWindowMaximized());
        }
    };
    window->SetOnResize([sync_maximize](int, int) { sync_maximize(); });
    window->SetSizeLimits(960, 620, 0, 0);
    window->SetFrameCallback([editor, sync_maximize] {
        editor->TickTree(1.0 / 30.0);
        sync_maximize();
    }, 33);
    window->Show();
    const int result = app.Run();
    app.Shutdown();
    // Browsers are children of the now-destroyed window — draining CEF
    // after the message loop lets it tear subprocesses down cleanly.
    XER::Cef::Shutdown();
    return result;
}

// `video_editor --cef-check` — headless verification of the whole CEF
// path the Browser tab uses: runtime init (libcef.dll + subprocess
// resolution) plus a real browser created on an invisible parent HWND.
// CefHost::Create blocks until Chromium has materialized the browser,
// so a true return already proves end-to-end creation; the short wait
// afterwards lets any crash in the fresh renderer/GPU subprocesses
// surface instead of exiting instantly. Exit 0 on success.
int RunCefCheck() {
    if (!XER::Cef::Available()) {
        std::cerr << "cef-check: CEF support not compiled in\n";
        return 1;
    }
    std::string error;
    if (!XER::Cef::Initialize({}, &error)) {
        std::cerr << "cef-check: init failed: " << error << "\n";
        return 1;
    }
    WNDCLASSW wc {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"XerCefCheckParent";
    RegisterClassW(&wc);
    // Real (non message-only) window — Chromium children cannot
    // parent under HWND_MESSAGE windows.
    HWND parent = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP,
                                  0, 0, 8, 8, nullptr, nullptr,
                                  wc.hInstance, nullptr);
    if (parent == nullptr) {
        std::cerr << "cef-check: parent window create failed\n";
        XER::Cef::Shutdown();
        return 1;
    }
    auto loaded = std::make_shared<std::atomic<bool>>(false);
    const std::weak_ptr<std::atomic<bool>> weak = loaded;
    XER::Cef::HostEvents events;
    events.on_loading = [weak](bool is_loading) {
        if (const auto s = weak.lock(); !is_loading) {
            s->store(true);
        }
    };
    XER::Cef::CefHost host;
    const bool created = host.Create(
        parent, 0, 0, 8, 8, "about:blank", std::move(events));
    if (!created) {
        std::cerr << "cef-check: browser create failed\n";
        DestroyWindow(parent);
        XER::Cef::Shutdown();
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(3);
    while (!loaded->load() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::cout << "cef-check: browser created"
              << (loaded->load() ? ", load event observed\n"
                                 : " (no load event — still OK)\n");
    host.Close();
    DestroyWindow(parent);
    XER::Cef::Shutdown();
    return 0;
}
#endif

const char* TrackKindName(TrackKind kind) {
    return kind == TrackKind::Video ? "video" : "audio";
}

int RunInfo(const std::string& input_path) {
    Project project;
    std::string error;
    const std::string normalized = Path::NormalizeUtf8Path(input_path);
    if (!XER::VideoEditor::ProjectStore::Load(normalized, &project,
                                              &error)) {
        std::cerr << "Error: cannot load '" << input_path
                  << "': " << (error.empty() ? "load failed" : error)
                  << '\n';
        return 1;
    }

    std::vector<int> clips_per_track(project.tracks.size(), 0);
    std::set<std::string> sources;
    for (const auto& clip : project.clips) {
        if (clip.track >= 0 &&
            clip.track < static_cast<int>(clips_per_track.size())) {
            ++clips_per_track[static_cast<std::size_t>(clip.track)];
        }
        if (clip.is_adjustment) {
            // Adjustment layers composite the stack below them —
            // no source to probe.
            sources.insert("generated:adjustment-layer");
        } else if (clip.source.kind != SourceKind::Media) {
            // Procedural clips have no file to probe; list the kind.
            const char* kind_name =
                clip.source.kind == SourceKind::ColorMatte
                    ? "color_matte"
                    : clip.source.kind == SourceKind::ColorBars
                          ? "color_bars"
                          : "text";
            sources.insert(std::string("generated:") + kind_name +
                           " " + clip.source.param);
        } else {
            sources.insert(clip.source_path);
        }
    }

    std::cout << "Project: " << input_path << '\n';
    std::cout << "Canvas: " << project.width << 'x' << project.height
              << " @ " << project.fps_num << '/' << project.fps_den
              << " fps\n";
    std::cout << "Background: rgba("
              << static_cast<int>(project.bg_color[0]) << ','
              << static_cast<int>(project.bg_color[1]) << ','
              << static_cast<int>(project.bg_color[2]) << ','
              << static_cast<int>(project.bg_color[3]) << ")\n";
    std::cout << "Duration: " << project.duration() << " s\n";
    std::cout << "Tracks:\n";
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        const auto& track = project.tracks[i];
        std::cout << "  [" << i << "] " << TrackKindName(track.kind)
                  << " \"" << track.name << "\""
                  << " muted=" << (track.muted ? 1 : 0)
                  << " hidden=" << (track.hidden ? 1 : 0)
                  << " clips=" << clips_per_track[i] << '\n';
    }
    std::cout << "Transitions: " << project.transitions.size() << '\n';
    std::cout << "Media:\n";
    for (const auto& source : sources) {
        if (source.rfind("generated:", 0) == 0) {
            std::cout << "  " << source << '\n';
            continue;
        }
        XER::Content::Video::VideoInfo info;
        std::string probe_error;
        const std::string normalized_source =
            Path::NormalizeUtf8Path(source);
        if (XER::Content::Video::ProbeVideo(normalized_source, &info,
                                            &probe_error)) {
            std::cout << "  " << source << ": " << info.width << 'x'
                      << info.height << " fps=" << info.fps
                      << " duration=" << info.duration_seconds
                      << " has_audio=" << (info.has_audio ? 1 : 0)
                      << " video=" << info.video_codec
                      << " audio=" << info.audio_codec << '\n';
        } else {
            std::cout << "  " << source << ": probe failed: "
                      << (probe_error.empty() ? "unavailable"
                                              : probe_error)
                      << '\n';
        }
    }
    return 0;
}

int RunExport(const std::string& input_path,
              const std::string& output_path) {
    Project project;
    std::string error;
    const std::string normalized_input =
        Path::NormalizeUtf8Path(input_path);
    if (!XER::VideoEditor::ProjectStore::Load(normalized_input, &project,
                                              &error)) {
        std::cerr << "Error: cannot load '" << input_path
                  << "': " << (error.empty() ? "load failed" : error)
                  << '\n';
        return 1;
    }
    // No TimelineHistory is bound in CLI mode — the
    // history.Clear()-before-load contract is trivially satisfied;
    // 1D's dialog owns the history-bound path.

    ExportOptions options;
    options.output_path = Path::NormalizeUtf8Path(output_path);

    ExportEngine engine;
    auto last_report = std::chrono::steady_clock::now() -
                       std::chrono::milliseconds(500);
    int last_percent = -1;
    const auto progress_cb =
        [&last_report, &last_percent](const ExportProgress& progress) {
            const int percent = static_cast<int>(
                std::clamp(progress.fraction, 0.0, 1.0) * 100.0 + 0.5);
            const auto now = std::chrono::steady_clock::now();
            if (percent == last_percent &&
                now - last_report < std::chrono::milliseconds(100) &&
                percent != 100) {
                return;
            }
            last_report = now;
            last_percent = percent;
            std::fprintf(stdout, "\r  export: %3d%% (%s)", percent,
                         progress.stage.c_str());
            std::fflush(stdout);
        };

    if (!engine.Start(project, options, progress_cb, &error)) {
        std::cerr << "Error: " << (error.empty() ? "export failed" : error)
                  << '\n';
        return 1;
    }
    engine.Wait();
    std::fprintf(stdout, "\n");

    const ExportProgress final_progress = engine.Progress();
    switch (engine.Status()) {
        case ExportStatus::kDone:
            std::cout << "exported " << final_progress.video_frames_done
                      << " frames -> " << output_path << '\n';
            return 0;
        case ExportStatus::kCancelled:
            std::cerr << "export cancelled\n";
            return 2;
        case ExportStatus::kError:
            std::cerr << "export failed: " << engine.Error() << '\n';
            return 1;
        default:
            std::cerr << "export failed: worker did not finish\n";
            return 1;
    }
}

}  // namespace

#if defined(_WIN32)
// `video_editor --audio-probe [file] [--seconds N]` — end-to-end audio
// diagnostic: encodes a 3 s sine (or uses the given media file), plays
// it through the REAL PreviewEngine -> AudioMixer -> WasapiSink chain
// and prints a verdict per link. `--seconds` <= 0 (the default) plays
// the clip on a loop FOREVER — Ctrl+C stops it and prints the stats;
// a positive N keeps the old bounded run. You should HEAR the tone;
// silence with all checks green points at the endpoint/volume, a red
// link names the broken stage.
namespace {
std::atomic<bool> g_probe_stop{false};
BOOL WINAPI ProbeCtrlHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
        event == CTRL_CLOSE_EVENT) {
        g_probe_stop.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}
}  // namespace

int RunAudioProbe(const std::string& media_path, double seconds) {
    namespace FFmpeg = XER::Bridge::FFmpeg;
    namespace VE = XER::VideoEditor;
    std::cout << std::unitbuf;   // unbuffered — survive a hard crash

    // 1. Encode a deterministic 3 s stereo sine WAV — guarantees a
    //    known-good source no matter what the user's file is.
    std::string path = media_path;
    std::string err;
    if (path.empty()) {
        path = (std::filesystem::temp_directory_path() /
                "xer_audio_probe.wav").string();
        FFmpeg::EncodeAudioParams enc;
        enc.output_path = path;
        enc.codec_name = "pcm_s16le";
        enc.sample_rate = 48000;
        enc.channels = 2;
        enc.sample_format = "s16";
        const int chunk = 1024;
        const std::size_t chunks =
            static_cast<std::size_t>(3 * 48000 / chunk);
        if (!FFmpeg::EncodeAudioStream(
                enc, chunks,
                [](std::size_t i, FFmpeg::AudioFrameInfo* out,
                   std::string*) {
                    out->sample_rate = 48000;
                    out->channels = 2;
                    out->sample_count = 1024;
                    out->planar = false;
                    out->sample_format = "s16";
                    out->data.resize(1024 * 4);
                    auto* s =
                        reinterpret_cast<std::int16_t*>(out->data.data());
                    for (int j = 0; j < 1024; ++j) {
                        const double t =
                            (i * 1024.0 + j) / 48000.0;
                        const auto v = static_cast<std::int16_t>(
                            std::sin(2 * 3.14159265358979 * 440.0 * t) *
                            12000);
                        s[j * 2] = v;
                        s[j * 2 + 1] = v;
                    }
                    return true;
                },
                &err)) {
            std::cerr << "probe[1/6] encode sine FAILED: " << err
                      << "\n";
            return 1;
        }
        std::cout << "probe[1/6] sine wav encoded -> " << path << "\n";
    } else {
        std::cout << "probe[1/6] using media: " << path << "\n";
    }

    // 2. Probe stream presence via the same code the import path uses.
    XER::Content::Video::VideoInfo info;
    if (!XER::Content::Video::ProbeVideo(path, &info, &err)) {
        std::cerr << "probe[2/6] ProbeVideo FAILED: " << err << "\n";
        return 1;
    }
    std::cout << "probe[2/6] streams: video="
              << (info.width > 0 ? "yes" : "no")
              << " audio=" << (info.has_audio ? "yes" : "no")
              << " dur=" << info.duration_seconds << "s\n";
    if (!info.has_audio) {
        std::cerr << "probe[2/6] FAIL: no audio stream detected\n";
        return 1;
    }

    // 3. Platform sink — the object the UI creates on its STA thread.
    auto sink = VE::MakePlatformAudioSink(&err);
    if (!sink) {
        std::cerr << "probe[3/6] sink create FAILED: " << err << "\n";
        return 1;
    }
    std::cout << "probe[3/6] platform audio sink created\n";

    // 4-5. Build a minimal project: one audio track + one media clip,
    //      then run the real PreviewEngine (compositor + mixer +
    //      worker) with a counting sink wrapper for ~3 s.
    struct CountSink : VE::IAudioSink {
        VE::IAudioSink* inner;
        std::atomic<int> writes{0}, fails{0}, nonzero{0};
        std::string last_err;
        bool Write(const std::int16_t* p, int n, int rate) override {
            ++writes;
            for (int i = 0; i < n * 2; ++i) {
                if (p[i] != 0) { ++nonzero; break; }
            }
            if (!inner->Write(p, n, rate)) {
                ++fails;
                last_err = inner->LastError();
                return false;
            }
            return true;
        }
        std::string LastError() const override { return last_err; }
    };
    struct NullSink : VE::PreviewEngine::Sink {
        void OnVideoFrame(const video::Frame&) override {}
        void OnAudioPeakLevels(float, float) override {}
        void OnPlayhead(double) override {}
    };

    VE::Project project;
    project.width = 64;
    project.height = 64;
    project.fps_num = 30;
    project.fps_den = 1;
    VE::Track track{VE::TrackKind::Audio, "A1"};
    project.tracks.push_back(track);
    VE::Clip clip;
    clip.id = "probe";
    clip.source_path = path;
    clip.track = 0;
    clip.timeline_start = 0.0;
    clip.in_point = 0.0;
    clip.duration = info.duration_seconds > 0.0
                        ? std::min(info.duration_seconds, 90.0)
                        : 90.0;
    project.clips.push_back(clip);

    CountSink counter;
    counter.inner = sink.get();
    NullSink null_sink;
    VE::PreviewOptions opts;
    opts.audio_sink = &counter;
    VE::PreviewEngine engine;
    if (!engine.Init(project, opts, &null_sink, &err)) {
        std::cerr << "probe[4/6] engine Init FAILED: " << err << "\n";
        return 1;
    }
    std::cout << "probe[4/6] preview engine up (dur="
              << engine.DurationSeconds() << "s)\n";

    // seconds <= 0: loop the clip and run until Ctrl+C. SetConsoleCtrl-
    // Handler intercepts the event so we stop gracefully and still
    // print the per-link stats.
    const bool infinite = seconds <= 0.0;
    if (infinite) {
        engine.SetLoop(0.0, clip.duration);
        SetConsoleCtrlHandler(ProbeCtrlHandler, TRUE);
        std::cout << "probe[5/6] looping " << clip.duration
                  << "s clip — press Ctrl+C to stop\n";
    }
    engine.SetPlaying(true);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(
                              static_cast<long long>(
                                  clip.duration * 1000.0) + 1500);
    while (!g_probe_stop.load(std::memory_order_relaxed) &&
           (infinite || std::chrono::steady_clock::now() < deadline) &&
           engine.Playing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    engine.Shutdown();
    if (infinite) {
        SetConsoleCtrlHandler(ProbeCtrlHandler, FALSE);
    }
    const auto warnings = engine.Warnings();
    std::cout << "probe[5/6] writes=" << counter.writes.load()
              << " fails=" << counter.fails.load()
              << " non-silent=" << counter.nonzero.load()
              << (counter.last_err.empty()
                      ? ""
                      : " last='" + counter.last_err + "'")
              << "\n";
    for (const auto& w : warnings) {
        std::cout << "  warn: " << w << "\n";
    }

    if (counter.fails.load() > 0) {
        std::cerr << "probe[6/6] FAIL: device writes rejected\n";
        return 1;
    }
    if (counter.writes.load() == 0) {
        std::cerr << "probe[6/6] FAIL: pump never wrote (mixer/pump "
                     "link dead)\n";
        return 1;
    }
    if (counter.nonzero.load() == 0) {
        std::cerr << "probe[6/6] FAIL: all chunks silent (mixer "
                     "decoded to zeros)\n";
        return 1;
    }
    std::cout << "probe[6/6] OK — audible 440Hz should have played; "
                 "if you heard nothing, check endpoint/volume\n";
    return 0;
}
#endif

#if defined(_WIN32)
// Process-wide last-chance reporter: prints module/RVA + a symbolized
// backtrace for SEH faults on ANY thread (the mixer/decoder worker
// crashes otherwise surface only as an abrupt exit / silent audio).
static LONG WINAPI VeCrashTrace(EXCEPTION_POINTERS* e) {
    void* addr = e->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    std::fprintf(stderr, "\n[CRASH] code=0x%08lx addr=%p\n",
                 e->ExceptionRecord->ExceptionCode, addr);
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &mod)) {
        char name[MAX_PATH]{};
        GetModuleFileNameA(mod, name, MAX_PATH);
        std::fprintf(stderr, "[CRASH] module=%s rva=%p\n", name,
                     reinterpret_cast<char*>(addr) -
                         reinterpret_cast<char*>(mod));
    }
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);
    void* stack[64];
    USHORT frames = CaptureStackBackTrace(0, 64, stack, nullptr);
    for (USHORT i = 0; i < frames; ++i) {
        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = MAX_SYM_NAME;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, reinterpret_cast<DWORD64>(stack[i]), &disp,
                        sym)) {
            HMODULE smod = nullptr;
            char mname[MAX_PATH]{};
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(stack[i]), &smod)) {
                GetModuleFileNameA(smod, mname, MAX_PATH);
            }
            std::fprintf(stderr, "  #%u %s!%s+0x%llx\n", i, mname,
                         sym->Name, disp);
        }
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(VeCrashTrace);
    // CEF child processes (renderer/GPU via bootstrap.exe) never return
    // from this — must run before any other initialization.
    const int cef_exit =
        XER::Cef::RunSubprocessIfAny(argc, argv);
    if (cef_exit >= 0) {
        return cef_exit;
    }
    const std::vector<std::string> args = CommandLineArgsUtf8();
    // Non-Debug builds link GUI-subsystem (-mwindows): no console is
    // allocated, so double-click/Explorer launches never flash a
    // terminal. When started FROM a terminal we still want --smoke,
    // --export, --audio-probe output — borrow the parent's console.
    // Skipped for --media-worker (stdout is the JSON-lines IPC pipe)
    // and CEF --type= children (handled above anyway).
    bool pipe_mode = false;
    for (const std::string& a : args) {
        if (a == "--media-worker" || a.rfind("--type=", 0) == 0) {
            pipe_mode = true;
        }
    }
    // stdout already redirected (pipe/file)? Leave it alone — output
    // is captured fine. Only when it goes nowhere (GUI launch with a
    // parent console) do we rebind to the parent's console.
    const DWORD stdout_type =
        GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    const bool stdout_redirected =
        stdout_type == FILE_TYPE_PIPE || stdout_type == FILE_TYPE_DISK;
    if (!pipe_mode && !stdout_redirected &&
        AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        fwide(stdin, 1);
        freopen_s(&f, "CONIN$", "r", stdin);
    }
    SetConsoleOutputCP(CP_UTF8);
    (void)argc;
    (void)argv;
#else
    std::vector<std::string> args(argv, argv + argc);
#endif
    if (args.size() < 2) {
#if defined(_WIN32)
        return RunGui({});
#else
        PrintUsage();
        return 1;
#endif
    }

    const std::string& arg = args[1];
    if (arg == "--help" || arg == "-h" || arg == "/?") {
        PrintUsage();
        return 0;
    }
#if defined(_WIN32)
    if (arg == "--media-worker") {
        // Headless JSON-lines decode worker — the parent spawns this
        // same exe to isolate poster/peaks decode crashes.
        return XER::VideoEditor::RunMediaWorker();
    }
    if (arg == "--audio-probe") {
        // --audio-probe [file] [--seconds N] — N <= 0 runs forever.
        std::string media;
        double seconds = -1.0;
        for (std::size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--seconds" && i + 1 < args.size()) {
                seconds = std::strtod(args[++i].c_str(), nullptr);
            } else if (media.empty()) {
                media = args[i];
            }
        }
        return RunAudioProbe(media, seconds);
    }
#endif
    if (arg == "--open" || arg == "--gui") {
        if (args.size() < 3) {
            std::cerr << "Error: " << arg << " requires a project path\n";
            return 1;
        }
#if defined(_WIN32)
        return RunGui(args[2]);
#else
        std::cerr << "Error: desktop editor is Windows-only\n";
        return 1;
#endif
    }
    if (arg == "--info") {
        if (args.size() < 3) {
            std::cerr << "Error: --info requires a project path\n";
            return 1;
        }
        return RunInfo(args[2]);
    }
    if (arg == "--export") {
        if (args.size() < 4) {
            std::cerr << "Error: --export requires input and output paths\n";
            return 1;
        }
        return RunExport(args[2], args[3]);
    }
    if (arg == "--cef-check") {
#if defined(_WIN32)
        return RunCefCheck();
#else
        std::cerr << "Error: --cef-check is Windows-only\n";
        return 1;
#endif
    }
    if (arg == "--smoke") {
        // Headless, bounded self-check — see smoke_checks.h.
        return XER::VideoEditor::Smoke::RunAll();
    }
    if (arg == "--smoke-list") {
        for (const std::string& name :
             XER::VideoEditor::Smoke::CheckNames()) {
            std::cout << name << '\n';
        }
        return 0;
    }

    if (!arg.empty() && arg.front() != '-') {
#if defined(_WIN32)
        return RunGui(arg);
#else
        std::cerr << "Error: desktop editor is Windows-only\n";
        return 1;
#endif
    }

    std::cerr << "Error: unknown argument '" << arg << "'\n";
    PrintUsage();
    return 1;
}
