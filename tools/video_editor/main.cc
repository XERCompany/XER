// video_editor.exe — XER Studio Phase 1C/1D executable.
//   video_editor [project.xerproj]                  Premiere-style GUI
//   video_editor --open <in.xerproj>                open project in GUI
//   video_editor --export <in.xerproj> <out.mp4>   headless render+encode
//   video_editor --info <in.xerproj>               project summary
//   video_editor --smoke                           headless self-checks
//   video_editor --smoke-list                      list check names
//   video_editor --help

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "content/video/video_info.h"
#include "export_engine.h"
#include "helper/path_utils.h"
#include "project_store.h"
#include "smoke_checks.h"
#include "timeline_model.h"

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>

#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/theme/theme.h"
#include "ui/video_editor_app.h"
#include "ui/widgets/app_title_bar.h"

#include "app_runtime/app_runtime.h"
#include "app_runtime/js_engine.h"  // complete type for BootResult dtor
#include "cef/cef_runtime.h"        // CEF subprocess early-out
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

int main(int argc, char** argv) {
#if defined(_WIN32)
    // CEF child processes (renderer/GPU via bootstrap.exe) never return
    // from this — must run before any other initialization.
    const int cef_exit =
        XER::Cef::RunSubprocessIfAny(argc, argv);
    if (cef_exit >= 0) {
        return cef_exit;
    }
    SetConsoleOutputCP(CP_UTF8);
    (void)argc;
    (void)argv;
    const std::vector<std::string> args = CommandLineArgsUtf8();
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
