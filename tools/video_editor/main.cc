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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "content/video/video_info.h"
#include "compositor.h"
#include "export_engine.h"
#include "frame_exporter.h"
#include "media_worker_server.h"
#include "helper/env_utils.h"   // XER_VE_UI2 switch
#include "helper/path_utils.h"
#include "media_catalog.h"
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
#include "ui/color_widgets_smoke.h"   // ColorWidgetsSmoke hook
#include "ui/data_widgets_smoke.h"    // DataWidgetsSmoke hook
#include "ui/media_widgets_smoke.h"   // MediaWidgetsSmoke hook
#include "ui/photo_workspace.h"       // PhotoWorkspaceSmoke hook
#include "ui/widget_kit_smoke.h"      // WidgetKitSmoke hook
#include "ui/video_editor_app.h"
#include "ui/widgets/app_title_bar.h"
// ui2/ — the rebuilt shell (parallel codebase, spec
// docs/wiki/architecture/ve-ui-rebuild-spec.md). Launched via --ui2
// or XER_VE_UI2=1; the legacy ui/ shell stays the default.
#include "ui2/ui2_shell_smoke.h"  // Ui2ShellSmoke hook
#include "ui2/ve_app.h"
#include "ui2/ve_theme.h"

#include "rendering/windows/software_renderer.h"  // --screenshot --ui
#include "theme/tokens.h"
#include "widgets/widget_tree.h"

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
              << "  video_editor --screenshot <in.xerproj> [--t sec] "
                 "[--out p.png] [--size WxH] [--ui]\n"
              << "                                            "
                 "Headless PNG: program frame at t, or full UI with --ui\n"
              << "  video_editor --audio-probe [file] [--seconds N]  "
                 "Play media through the real audio path (N<=0: loop)\n"
              << "  video_editor --diag                           "
                 "Print a paste-able diagnostic report, then exit\n"
              << "  video_editor --media-worker                   "
                 "Headless decode worker (spawned by the editor)\n"
              << "  video_editor --smoke                          "
                 "Run headless self-checks (SMOKE <name> OK/FAIL)\n"
              << "  video_editor --cef-check                      "
                 "Headless CEF bring-up (runtime init + browser)\n"
              << "  video_editor --smoke-list                     "
                 "List the smoke check names\n"
              << "  video_editor --ui2 [project]                  "
                 "Launch the rebuilt ui2 shell (or XER_VE_UI2=1)\n"
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
    app.SetAppName("VideoEditor - XER Creative Suite");
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
    // Same init options the Browser panel uses — the check must cover
    // the code path users actually hit.
    XER::Cef::RuntimeOptions cef_options;
    cef_options.disable_gpu = true;
    if (!XER::Cef::Initialize(cef_options, &error)) {
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

// ---- --screenshot -------------------------------------------------

// "WxH" flag value -> size. Returns false on malformed/out-of-range
// input so the caller can report a flag error instead of rendering.
bool ParseWxH(const std::string& text, int* width, int* height) {
    const std::size_t x = text.find_first_of("xX");
    if (x == std::string::npos || x == 0 || x + 1 >= text.size()) {
        return false;
    }
    char* end = nullptr;
    const long w = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str() + x) return false;
    const long h = std::strtol(text.c_str() + x + 1, &end, 10);
    if (end == nullptr || *end != '\0') return false;
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return false;
    *width = static_cast<int>(w);
    *height = static_cast<int>(h);
    return true;
}

// Bilinear BGRA8 resample — applied only when --size differs from the
// project canvas (identity sizes skip it entirely).
void ResizeBgraBilinear(const std::vector<std::uint8_t>& src, int sw,
                      int sh, std::vector<std::uint8_t>* dst, int dw,
                      int dh) {
    dst->assign(static_cast<std::size_t>(dw) * dh * 4, 0);
    for (int y = 0; y < dh; ++y) {
        const double fy = std::clamp(
            (y + 0.5) * static_cast<double>(sh) / dh - 0.5, 0.0,
            static_cast<double>(sh - 1));
        const int y0 = static_cast<int>(fy);
        const int y1 = std::min(y0 + 1, sh - 1);
        const double wy = fy - y0;
        for (int x = 0; x < dw; ++x) {
            const double fx = std::clamp(
                (x + 0.5) * static_cast<double>(sw) / dw - 0.5, 0.0,
                static_cast<double>(sw - 1));
            const int x0 = static_cast<int>(fx);
            const int x1 = std::min(x0 + 1, sw - 1);
            const double wx = fx - x0;
            for (int c = 0; c < 4; ++c) {
                const double p00 = src[(static_cast<std::size_t>(y0) * sw + x0) * 4 + c];
                const double p01 = src[(static_cast<std::size_t>(y0) * sw + x1) * 4 + c];
                const double p10 = src[(static_cast<std::size_t>(y1) * sw + x0) * 4 + c];
                const double p11 = src[(static_cast<std::size_t>(y1) * sw + x1) * 4 + c];
                const double top = p00 + (p01 - p00) * wx;
                const double bot = p10 + (p11 - p10) * wx;
                (*dst)[(static_cast<std::size_t>(y) * dw + x) * 4 + c] =
                    static_cast<std::uint8_t>(
                        std::clamp(top + (bot - top) * wy + 0.5, 0.0,
                                   255.0));
            }
        }
    }
}

// Wraps packed BGRA8 pixels in a video::Frame and writes a PNG through
// the same FFmpeg encoder path export/poster extraction uses.
bool SaveBgraPng(const std::uint8_t* bgra, int w, int h,
                 const std::string& utf8_path, std::string* error_out) {
    video::Frame frame;
    if (!frame.Allocate(w, h, video::PixelFormat::BGRA8)) {
        if (error_out != nullptr) *error_out = "frame allocation failed";
        return false;
    }
    std::memcpy(frame.Data(), bgra,
                static_cast<std::size_t>(w) * h * 4);
    video::FrameExporter exporter;
    if (!exporter.ToFile(utf8_path, frame)) {
        if (error_out != nullptr) {
            *error_out = "PNG encode/write failed for " + utf8_path;
        }
        return false;
    }
    return true;
}

// --bench-effects — dev micro-benchmark for the heavy clip effects.
// Renders a synthetic 1920x1080 generated frame through each glass /
// bokeh kernel (via the real Compositor::RenderFrameAt path) at Full
// / 1/2 / 1/4 preview quality and prints a ms/frame table (best of
// N timed renders after one warmup). Hidden flag — not in --help.
int RunBenchEffects() {
    namespace VE = XER::VideoEditor;
    struct Case {
        const char* type;
        std::vector<float> params;
    };
    // Representative params — same ballpark as the effects-panel
    // presets (big enough radii to stress the kernels).
    const Case cases[] = {
        {"lens_sphere", {0.8f, 1.5f, 2.0f, 0.3f, 0.5f}},
        {"magnifier", {0.4f, 2.0f, 3.0f}},
        {"ribbed_glass", {16.0f, 3.0f, 0.0f, 0.3f}},
        {"bokeh_dof", {1.0f, 0.5f, 0.5f, 0.12f, 0.3f, 18.0f, 3.0f}},
        {"bokeh_blur", {10.0f, 2.5f}},
    };
    constexpr int kTimedFrames = 5;
    constexpr float kQualities[] = {1.0f, 0.5f, 0.25f};
    std::printf("%-14s %12s %12s %12s\n", "effect", "full ms/f",
                "half ms/f", "quarter ms/f");
    for (const Case& c : cases) {
        double best[3] = {0.0, 0.0, 0.0};
        for (int qi = 0; qi < 3; ++qi) {
            VE::Project project;
            project.width = 1920;
            project.height = 1080;
            project.fps_num = 30;
            project.fps_den = 1;
            project.tracks.push_back(VE::Track{VE::TrackKind::Video, "V1"});
            VE::Clip clip;
            clip.id = "bars";
            clip.track = 0;
            clip.timeline_start = 0.0;
            clip.duration = 1.0;
            clip.source.kind = VE::SourceKind::ColorBars;
            clip.source.param = "smpte75";
            clip.effects.push_back({c.type, c.params});
            project.clips.push_back(clip);

            VE::Compositor comp;
            std::string err;
            if (!comp.Init(project, {}, &err)) {
                std::fprintf(stderr, "bench: init failed: %s\n",
                             err.c_str());
                return 1;
            }
            comp.SetPreviewQuality(kQualities[qi]);
            std::vector<std::uint8_t> bgra;
            // Warmup: frame buffers + generated-source caches allocate
            // on the first render — excluded from the timing.
            if (!comp.RenderFrameAt(0.5, &bgra, &err)) {
                std::fprintf(stderr, "bench: warmup failed: %s\n",
                             err.c_str());
                return 1;
            }
            double ms = std::numeric_limits<double>::max();
            for (int i = 0; i < kTimedFrames; ++i) {
                const auto t0 = std::chrono::steady_clock::now();
                if (!comp.RenderFrameAt(0.5, &bgra, &err)) {
                    std::fprintf(stderr, "bench: render failed: %s\n",
                                 err.c_str());
                    return 1;
                }
                const double elapsed =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
                ms = std::min(ms, elapsed);
            }
            best[qi] = ms;
            comp.Shutdown();
        }
        std::printf("%-14s %12.1f %12.1f %12.1f\n", c.type, best[0],
                    best[1], best[2]);
        std::fflush(stdout);
    }
    return 0;
}

// Program-frame capture: ProjectStore::Load -> Compositor ->
// RenderFrameAt(t) -> optional --size resample -> PNG. Fully headless
// (no window, no UI libs); identical to the --export render path.
int RunScreenshotProgram(const std::string& project_path, double t,
                         const std::string& out_path, int size_w,
                         int size_h) {
    Project project;
    std::string error;
    const std::string normalized = Path::NormalizeUtf8Path(project_path);
    if (!XER::VideoEditor::ProjectStore::Load(normalized, &project,
                                              &error)) {
        std::cerr << "Error: cannot load '" << project_path
                  << "': " << (error.empty() ? "load failed" : error)
                  << '\n';
        return 1;
    }

    XER::VideoEditor::Compositor compositor;
    if (!compositor.Init(project, {}, &error)) {
        std::cerr << "Error: compositor init failed: " << error << '\n';
        return 1;
    }
    std::vector<std::uint8_t> bgra;
    const bool rendered = compositor.RenderFrameAt(t, &bgra, &error);
    const int canvas_w = compositor.CanvasWidth();
    const int canvas_h = compositor.CanvasHeight();
    compositor.Shutdown();
    if (!rendered) {
        std::cerr << "Error: render failed: " << error << '\n';
        return 1;
    }

    int out_w = canvas_w, out_h = canvas_h;
    if (size_w > 0 && size_h > 0 &&
        (size_w != canvas_w || size_h != canvas_h)) {
        std::vector<std::uint8_t> resized;
        ResizeBgraBilinear(bgra, canvas_w, canvas_h, &resized, size_w,
                           size_h);
        bgra.swap(resized);
        out_w = size_w;
        out_h = size_h;
    }
    if (!SaveBgraPng(bgra.data(), out_w, out_h, out_path, &error)) {
        std::cerr << "Error: " << error << '\n';
        return 1;
    }
    std::cout << "screenshot: " << out_path << ' ' << out_w << 'x'
              << out_h << " ok\n";
    return 0;
}

#if defined(_WIN32)
// Full-UI capture: the real VideoEditorAppWidget laid out into a
// headless SoftwareRenderer (CPU BGRA rasterizer — no window, no GPU)
// through a standalone WidgetTree, then read back via Pixels(). The
// monitor shows the composited frame at `t` when the preview engine
// delivers (PrepareUiCapture); chrome/panels/timeline always render.
int RunScreenshotUi(const std::string& project_path, double t,
                    const std::string& out_path, int size_w,
                    int size_h) {
    // Declarative assets layer first — EnsureChildren reads
    // SharedLayoutConfig()/SharedAssetsConfig() seeded by Boot.
    std::string boot_status;
    auto boot = XER::VideoEditor::AppRuntime::Boot(&boot_status);
    if (!boot.ok) {
        std::cerr << "assets: " << boot_status << '\n';
    }
    int width = size_w > 0 ? size_w : boot.window.width;
    int height = size_h > 0 ? size_h : boot.window.height;
    // The app's declared minimum is 960x620 — smaller captures would
    // clip panels; clamp up rather than fail.
    width = std::clamp(width, 960, 16384);
    height = std::clamp(height, 620, 16384);

    const std::string normalized = Path::NormalizeUtf8Path(project_path);
    if (!std::filesystem::exists(Path::FromUtf8(normalized))) {
        std::cerr << "Error: project not found: " << project_path
                  << '\n';
        return 1;
    }

    XER::UI::Widgets::WidgetTree tree;   // outlives `app` — see below
    auto app =
        std::make_shared<XER::VideoEditor::Ui::VideoEditorAppWidget>(
            nullptr, normalized);  // null HWND: ctor skips subclassing
    tree.SetRoot(app);

    const XER::UI::Theme::Tokens tokens =
        XER::VideoEditor::Ui::VeTheme::DarkTokens(
            XER::VideoEditor::AppRuntime::SharedAssetsConfig()
                .editor.accent_argb);
    XER::UI::Rendering::Windows::SoftwareRenderer renderer(nullptr);
    renderer.SetDpi(96);   // 1 DIP == 1 px: --size is the pixel size
    renderer.SetClearColor(tokens.background);
    const XER::UI::Layout::Rect bounds{
        0.0f, 0.0f, static_cast<float>(width),
        static_cast<float>(height)};
    try {
        tree.Layout(bounds, tokens);
        // Pump the engine so the program monitor holds the frame at
        // `t`; degrade quietly to the placeholder on headless audio.
        app->PrepareUiCapture(t, 3000);
        // Two render passes: the first warms lazy per-widget state
        // (monitor frame resource, scroll metrics), the second draws
        // the settled tree into the retained buffer.
        for (int pass = 0; pass < 2; ++pass) {
            app->TickTree(1.0 / 30.0);
            tree.Layout(bounds, tokens);
            tree.Render(renderer, tokens, nullptr);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: UI render failed: " << e.what() << '\n';
        tree.SetRoot(nullptr);
        app.reset();
        return 1;
    }

    const std::uint32_t* pixels = renderer.Pixels();
    const int px_w = renderer.PixelWidth();
    const int px_h = renderer.PixelHeight();
    std::string error;
    bool saved = false;
    if (pixels != nullptr && px_w > 0 && px_h > 0) {
        saved = SaveBgraPng(
            reinterpret_cast<const std::uint8_t*>(pixels), px_w, px_h,
            out_path, &error);
    } else {
        error = "renderer produced no pixels";
    }
    // Teardown order matters: the widget's dtor path (RemoveChild ->
    // Invalidate) still walks tree_ — keep the WidgetTree alive until
    // the app widget is fully destroyed, then drop the tree.
    tree.SetRoot(nullptr);
    app.reset();
    if (!saved) {
        std::cerr << "Error: " << error << '\n';
        return 1;
    }
    std::cout << "screenshot: " << out_path << ' ' << px_w << 'x'
              << px_h << " ok\n";
    return 0;
}
#endif

// --screenshot <project.xerproj> [--t <sec>] [--out shot.png]
//                                [--size WxH] [--ui]
int RunScreenshot(const std::vector<std::string>& args) {
    const std::string project_path = args[2];
    double t = 0.0;   // no playhead is persisted in .xerproj
    int size_w = 0, size_h = 0;
    bool ui_mode = false;
    // Default output: next to the project, <stem>.png (never a bare
    // CWD dump that collides between projects).
    std::string out_path;
    for (std::size_t i = 3; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--ui") {
            ui_mode = true;
        } else if (a == "--t" && i + 1 < args.size()) {
            t = std::strtod(args[++i].c_str(), nullptr);
            if (!std::isfinite(t)) {
                std::cerr << "Error: --t requires a finite seconds "
                             "value\n";
                return 1;
            }
        } else if (a == "--out" && i + 1 < args.size()) {
            out_path = args[++i];
        } else if (a == "--size" && i + 1 < args.size()) {
            if (!ParseWxH(args[++i], &size_w, &size_h)) {
                std::cerr << "Error: --size requires WxH "
                             "(e.g. 1280x720)\n";
                return 1;
            }
        } else {
            std::cerr << "Error: unknown --screenshot option '" << a
                      << "'\n";
            return 1;
        }
    }
    if (out_path.empty()) {
        const std::filesystem::path proj =
            Path::FromUtf8(Path::NormalizeUtf8Path(project_path));
        out_path = Path::NormalizeUtf8Path(
            (proj.parent_path() /
             (proj.stem().generic_string() + ".png"))
                .generic_string());
    }
    if (t < 0.0) t = 0.0;

#if defined(_WIN32)
    if (ui_mode) {
        return RunScreenshotUi(project_path, t, out_path, size_w,
                               size_h);
    }
#else
    if (ui_mode) {
        std::cerr << "Error: --screenshot --ui is Windows-only\n";
        return 1;
    }
#endif
    return RunScreenshotProgram(project_path, t, out_path, size_w,
                                size_h);
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
// `video_editor --diag` — prints a paste-able bug-report block to
// stdout and exits without opening the UI. Every section is
// fault-tolerant: a failure prints the error and the report
// continues; only a hard crash (handled by VeCrashTrace) prevents a
// clean exit.
namespace {

template <typename F>
void DiagSection(const char* name, F&& fn) {
    std::cout << "\n[" << name << "]\n";
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "  error: " << e.what() << "\n";
    } catch (...) {
        std::cout << "  error: unknown exception\n";
    }
}

// Reads the delay-import directory of a PE file on disk and returns
// its delay-loaded DLL names. The exe's own table is usually empty —
// the real delay-loads (OCIO/OIIO/ggml/whisper) live in the YA.*.dll
// module siblings. Header/section walk only; bounded reads.
std::vector<std::string> CollectDelayLoadNames(
    const std::filesystem::path& pe) {
    std::vector<std::string> names;
    std::ifstream f(pe, std::ios::binary);
    if (!f) return names;
    const auto read_at = [&f](std::uint64_t off, void* dst,
                              std::size_t n) {
        f.seekg(static_cast<std::streamoff>(off), std::ios::beg);
        f.read(static_cast<char*>(dst),
               static_cast<std::streamsize>(n));
        return f.good();
    };
    std::uint32_t nt_off = 0;
    if (!read_at(0x3C, &nt_off, 4)) return names;
    std::uint32_t sig = 0;
    if (!read_at(nt_off, &sig, 4) || sig != 0x00004550u /* "PE\0\0" */) {
        return names;
    }
    std::uint16_t num_sections = 0, opt_size = 0;
    if (!read_at(nt_off + 6, &num_sections, 2) ||
        !read_at(nt_off + 20, &opt_size, 2)) {
        return names;
    }
    const std::uint64_t opt_off = nt_off + 24;
    std::uint16_t magic = 0;
    if (!read_at(opt_off, &magic, 2)) return names;
    const std::uint64_t dd_base = magic == 0x20B ? 112 : 96;
    std::uint32_t dir_rva = 0;
    if (!read_at(opt_off + dd_base + 13 * 8, &dir_rva, 4) ||
        dir_rva == 0) {
        return names;
    }
    struct Sec { std::uint32_t vsize, va, raw; };
    std::vector<Sec> secs(num_sections);
    const std::uint64_t sec_off = opt_off + opt_size;
    for (std::size_t i = 0; i < secs.size(); ++i) {
        char hdr[40];
        if (!read_at(sec_off + i * sizeof(hdr), hdr, sizeof(hdr))) {
            return names;
        }
        std::memcpy(&secs[i].vsize, hdr + 8, 4);
        std::memcpy(&secs[i].va, hdr + 12, 4);
        std::memcpy(&secs[i].raw, hdr + 20, 4);
    }
    const auto rva_to_off = [&secs](std::uint32_t rva) -> std::uint64_t {
        for (const auto& s : secs) {
            if (rva >= s.va && rva < s.va + s.vsize) {
                return s.raw + static_cast<std::uint64_t>(rva - s.va);
            }
        }
        return 0;
    };
    const std::uint64_t dd_off = rva_to_off(dir_rva);
    if (dd_off == 0) return names;
    // ImgDelayDescr entries (8 x u32), null-terminated. attrs bit 0
    // (dlattrRva) marks fields as RVAs — always set for lld output.
    for (int i = 0; i < 128; ++i) {
        char d[32];
        if (!read_at(dd_off + static_cast<std::uint64_t>(i) * 32, d,
                     sizeof(d))) {
            break;
        }
        std::uint32_t attrs = 0, name_rva = 0;
        std::memcpy(&attrs, d, 4);
        std::memcpy(&name_rva, d + 4, 4);
        if (name_rva == 0 || (attrs & 1) == 0) break;
        const std::uint64_t name_off = rva_to_off(name_rva);
        if (name_off == 0) break;
        char name[260];
        if (!read_at(name_off, name, sizeof(name) - 1)) break;
        name[sizeof(name) - 1] = '\0';
        names.emplace_back(name);
    }
    return names;
}

}  // namespace

int RunDiag() {
    namespace FFmpeg = XER::Bridge::FFmpeg;
    namespace VE = XER::VideoEditor;
    namespace AppRuntime = XER::VideoEditor::AppRuntime;
    std::cout << std::unitbuf;   // unbuffered — survive a hard crash

    // Own image/path — used by build, cef and runtime sections.
    std::wstring wexe(32768, L'\0');
    const DWORD wexe_len =
        GetModuleFileNameW(nullptr, wexe.data(),
                           static_cast<DWORD>(wexe.size()));
    wexe.resize(wexe_len);
    const std::string exe_path = WideArgToUtf8(wexe.c_str());
    const std::filesystem::path exe_dir =
        std::filesystem::path(wexe).parent_path();
    const auto* image =
        reinterpret_cast<const char*>(GetModuleHandleW(nullptr));
    const IMAGE_NT_HEADERS* nt = nullptr;
    if (image != nullptr) {
        const auto* dos =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const auto* cand =
                reinterpret_cast<const IMAGE_NT_HEADERS*>(
                    image + dos->e_lfanew);
            if (cand->Signature == IMAGE_NT_SIGNATURE) {
                nt = cand;
            }
        }
    }
    const auto dll_present = [&exe_dir](const std::string& name) {
        std::error_code ec;
        return std::filesystem::exists(exe_dir / name, ec);
    };

    std::cout << "=== video_editor --diag ===\n";

    DiagSection("build", [&] {
        std::cout << "  exe=" << exe_path << "\n";
        unsigned subsystem = 0;
        if (nt != nullptr) {
            subsystem = nt->OptionalHeader.Subsystem;
        }
        const char* subsystem_name =
            subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI ? "GUI"
            : subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI ? "CONSOLE"
                                                     : "?";
        std::cout << "  subsystem=" << subsystem_name << " ("
                  << subsystem << ")\n";
        // Version resource, if the linker embedded one. Read via
        // LoadLibrary-as-datafile + raw resource bytes (kernel32 only
        // — GetFileVersionInfo* would need version.lib, which the
        // mingw link line does not carry) and a signature scan for
        // the VS_FIXEDFILEINFO block.
        std::string version = "no version resource";
        if (HMODULE img = LoadLibraryExW(
                wexe.c_str(), nullptr,
                LOAD_LIBRARY_AS_DATAFILE |
                    LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {
            if (HRSRC res = FindResourceW(img, MAKEINTRESOURCEW(1),
                                          RT_VERSION)) {
                if (HGLOBAL mem = LoadResource(img, res)) {
                    const auto* words = static_cast<const DWORD*>(
                        LockResource(mem));
                    const DWORD dwords = SizeofResource(img, res) / 4;
                    for (DWORD i = 0; words != nullptr &&
                                      i + 13 <= dwords; ++i) {
                        if (words[i] == 0xFEEF04BD) {
                            const auto* fi =
                                reinterpret_cast<const VS_FIXEDFILEINFO*>(
                                    words + i);
                            version =
                                std::to_string(HIWORD(fi->dwFileVersionMS)) +
                                "." +
                                std::to_string(LOWORD(fi->dwFileVersionMS)) +
                                "." +
                                std::to_string(HIWORD(fi->dwFileVersionLS)) +
                                "." +
                                std::to_string(LOWORD(fi->dwFileVersionLS));
                            break;
                        }
                    }
                }
            }
            FreeLibrary(img);
        }
        std::cout << "  version=" << version << "\n";
#if defined(__clang__)
        std::cout << "  compiler=clang " << __clang_version__ << "\n";
#elif defined(_MSC_VER)
        std::cout << "  compiler=msvc " << _MSC_VER << "\n";
#endif
        std::string features;
        const auto flag = [&features](const char* n) {
            if (!features.empty()) features += ' ';
            features += n;
        };
#if defined(XER_VE_HAS_V8)
        flag("XER_VE_HAS_V8");
#endif
#if defined(XER_VE_HAS_OCIO)
        flag("XER_VE_HAS_OCIO");
#endif
#if defined(XER_VE_HAS_OIIO)
        flag("XER_VE_HAS_OIIO");
#endif
#if defined(XER_VE_HAS_RSVG)
        flag("XER_VE_HAS_RSVG");
#endif
#if defined(XER_VE_HAS_YAMLCPP)
        flag("XER_VE_HAS_YAMLCPP");
#endif
#if defined(XER_HAS_CEF)
        flag("XER_HAS_CEF");
#endif
        if (features.empty()) features = "(none)";
        std::cout << "  features=" << features << "\n";
    });

    DiagSection("assets", [&] {
        std::string status;
        auto boot = AppRuntime::Boot(&status);
        std::cout << "  boot=" << (boot.ok ? "ok" : "failed")
                  << (status.empty() ? "" : " status=\"" + status + "\"")
                  << (boot.error.empty() ? "" : " error=" + boot.error)
                  << "\n";
        const auto& dir = AppRuntime::SharedAssetsConfig().assets_dir;
        std::cout << "  assets_dir="
                  << (dir.empty() ? std::string("(unresolved)")
                                  : dir.generic_string())
                  << "\n";
    });

    DiagSection("audio", [&] {
        // Init-only probe: create the same platform sink the UI uses
        // (eagerly opens the WASAPI endpoint at 48 kHz) and push one
        // silent chunk to prove the device accepts writes — no audible
        // playback, no engine spin-up.
        std::string err;
        auto sink = VE::MakePlatformAudioSink(&err);
        if (!sink) {
            std::cout << "  sink=FAILED: "
                      << (err.empty() ? "unavailable" : err) << "\n";
            return;
        }
        std::cout << "  sink=ok (platform WASAPI sink created)\n";
        std::int16_t silence[480 * 2] = {};
        if (sink->Write(silence, 480, 48000)) {
            std::cout << "  write_test=ok\n";
        } else {
            std::cout << "  write_test=FAILED: "
                      << (sink->LastError().empty() ? "write rejected"
                                                    : sink->LastError())
                      << "\n";
        }
    });

    DiagSection("ffmpeg", [&] {
        std::cout << "  " << FFmpeg::Summary() << "\n";
        if (FFmpeg::IsAvailable()) {
            for (const auto& v : FFmpeg::LibraryVersions()) {
                std::cout << "  " << v.name << "=" << v.version_text
                          << "\n";
            }
        }
    });

    DiagSection("cef", [&] {
        std::cout << "  compiled="
                  << (XER::Cef::Available() ? "yes" : "no") << "\n";
        std::cout << "  libcef.dll="
                  << (dll_present("libcef.dll") ? "present" : "MISSING")
                  << "\n";
        std::cout << "  bootstrap.exe="
                  << (dll_present("bootstrap.exe") ? "present"
                                                 : "MISSING")
                  << "\n";
    });

    DiagSection("media", [&] {
        VE::MediaCatalog catalog;
        std::cout << "  db=" << catalog.DbPath() << "\n";
        std::string err;
        if (!catalog.Open(&err)) {
            std::cout << "  open=FAILED: "
                      << (err.empty() ? "unknown" : err) << "\n";
            return;
        }
        std::string list_err;
        const auto rows = catalog.List(&list_err);
        std::cout << "  open=ok rows=" << rows.size()
                  << (list_err.empty()
                          ? ""
                          : " list_error=" + list_err)
                  << "\n";
    });

    DiagSection("runtime", [&] {
        // Delay-load DLLs from own PE + YA.* module siblings,
        // presence-checked next to the exe — a missing entry here
        // means a LoadLibrary-probed feature will degrade or crash at
        // first use.
        std::map<std::string, std::string> dll_to_module;
        const auto harvest = [&](const std::filesystem::path& pe) {
            const std::string mod =
                pe.filename().generic_string();
            for (const std::string& n : CollectDelayLoadNames(pe)) {
                dll_to_module.emplace(n, mod);
            }
        };
        harvest(std::filesystem::path(wexe));
        std::error_code ec;
        for (const auto& e :
             std::filesystem::directory_iterator(exe_dir, ec)) {
            const std::string fn =
                e.path().filename().generic_string();
            if (fn.rfind("YA.", 0) == 0 && fn.size() > 4 &&
                fn.compare(fn.size() - 4, 4, ".dll") == 0) {
                harvest(e.path());
            }
        }
        int present = 0, missing = 0;
        for (const auto& [name, mod] : dll_to_module) {
            const bool ok = dll_present(name);
            std::cout << "  " << (ok ? "ok      " : "MISSING ") << name
                      << "  <- " << mod << "\n";
            if (ok) {
                ++present;
            } else {
                ++missing;
            }
        }
        std::cout << "  delay-loads: present=" << present
                  << " missing=" << missing << "\n";
        // Known optional runtime DLLs (LoadLibrary-probed, not
        // necessarily delay-loaded).
        for (const char* opt : {"libskia.dll", "libcef.dll",
                                "libwhisper.dll", "whisper.dll",
                                "libwhisper-1.dll"}) {
            if (dll_present(opt)) {
                std::cout << "  ok      " << opt << " (optional)\n";
            }
        }
        int ggml = 0, opencv = 0;
        for (const auto& e :
             std::filesystem::directory_iterator(exe_dir, ec)) {
            const std::string fn =
                e.path().filename().generic_string();
            if (fn.rfind("ggml", 0) == 0) ++ggml;
            if (fn.find("opencv") != std::string::npos) ++opencv;
        }
        std::cout << "  optional-dir-dlls: ggml*=" << ggml
                  << " opencv*=" << opencv << "\n";
    });

    DiagSection("smoke", [&] {
        std::string names;
        for (const std::string& n : VE::Smoke::CheckNames()) {
            if (!names.empty()) names += ", ";
            names += n;
        }
        std::cout << "  checks=" << names << "\n";
    });

    DiagSection("env", [&] {
        bool any = false;
        if (LPWCH block = GetEnvironmentStringsW()) {
            for (LPWCH p = block; *p != L'\0'; p += wcslen(p) + 1) {
                if (*p == L'=') continue;   // "=C:=..." drive vars
                const std::string entry = WideArgToUtf8(p);
                const auto eq = entry.find('=');
                const std::string name = entry.substr(0, eq);
                if (name.rfind("XER_", 0) == 0 || name == "OCIO" ||
                    name.rfind("VST", 0) == 0) {
                    std::cout << "  " << entry << "\n";
                    any = true;
                }
            }
            FreeEnvironmentStringsW(block);
        }
        if (!any) std::cout << "  (none set)\n";
    });

    std::cout << "\n=== end diag ===\n";
    return 0;
}

// `--ui2` / XER_VE_UI2=1: boots the rebuilt shell (ui2/ve_app.h) on
// the same window/app bootstrap as RunGui. The old ui/ shell stays
// untouched and default; the swap-over is a later milestone.
bool UseUi2Shell() {
    const std::string env = XER::Helper::GetEnv("XER_VE_UI2");
    return env == "1" || env == "true" || env == "TRUE";
}

int RunGui2(const std::string& project_path) {
    // Same assets bootstrap as the legacy path.
    std::string boot_status;
    auto boot = XER::VideoEditor::AppRuntime::Boot(&boot_status);
    if (!boot_status.empty()) {
        std::cerr << "assets: " << boot_status << "\n";
    }
    const auto& wcfg = boot.window;

    auto& app = XER::UI::Core::Application::Instance();
    app.SetAppName("VideoEditor - XER Creative Suite (ui2)");
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
    auto editor =
        std::make_shared<XER::VideoEditor::Ui2::VeApp>(project_path);
    window->SetContent(editor);
    window->SetTheme(XER::UI::Theme::Variant::Dark);
    XER::VideoEditor::Ui2::Theme::ApplyToWindow(
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
                XER::VideoEditor::Ui2::Theme::ApplyToWindow(
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
    XER::Cef::Shutdown();
    return result;
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
        return UseUi2Shell() ? RunGui2({}) : RunGui({});
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
    if (arg == "--diag") {
        // Paste-able bug-report block; exits without opening the UI.
        return RunDiag();
    }
#endif
    if (arg == "--ui2") {
        // Rebuilt shell (ui2/). Optional project path follows.
#if defined(_WIN32)
        const std::string project =
            args.size() >= 3 && !args[2].empty() &&
                    args[2].front() != '-'
                ? args[2]
                : std::string{};
        return RunGui2(project);
#else
        std::cerr << "Error: desktop editor is Windows-only\n";
        return 1;
#endif
    }
    if (arg == "--open" || arg == "--gui") {
        if (args.size() < 3) {
            std::cerr << "Error: " << arg << " requires a project path\n";
            return 1;
        }
#if defined(_WIN32)
        return UseUi2Shell() ? RunGui2(args[2]) : RunGui(args[2]);
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
    if (arg == "--screenshot") {
        if (args.size() < 3) {
            std::cerr << "Error: --screenshot requires a project path\n";
            return 1;
        }
        return RunScreenshot(args);
    }
    if (arg == "--cef-check") {
#if defined(_WIN32)
        return RunCefCheck();
#else
        std::cerr << "Error: --cef-check is Windows-only\n";
        return 1;
#endif
    }
    if (arg == "--bench-effects") {
        // Hidden dev flag: heavy-effect ms/frame micro-benchmark.
        return RunBenchEffects();
    }
    if (arg == "--smoke") {
        // Headless, bounded self-check — see smoke_checks.h.
#if defined(_WIN32)
        // Exe-side registration: the photo-workspace check exercises
        // widget classes that live in this target, not the UI-free
        // core where smoke_checks.cc compiles.
        XER::VideoEditor::Smoke::RegisterCheck(
            "photo_workspace",
            &XER::VideoEditor::Ui::PhotoWorkspaceSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "color_widgets",
            &XER::VideoEditor::Ui::ColorWidgetsSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "data_widgets",
            &XER::VideoEditor::Ui::DataWidgetsSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "widget_kit",
            &XER::VideoEditor::Ui::WidgetKitSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "media_widgets",
            &XER::VideoEditor::Ui::MediaWidgetsSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "ui2_shell",
            &XER::VideoEditor::Ui2::Ui2ShellSmoke);
#endif
        return XER::VideoEditor::Smoke::RunAll();
    }
    if (arg == "--smoke-list") {
#if defined(_WIN32)
        XER::VideoEditor::Smoke::RegisterCheck(
            "photo_workspace",
            &XER::VideoEditor::Ui::PhotoWorkspaceSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "color_widgets",
            &XER::VideoEditor::Ui::ColorWidgetsSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "data_widgets",
            &XER::VideoEditor::Ui::DataWidgetsSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "widget_kit",
            &XER::VideoEditor::Ui::WidgetKitSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "media_widgets",
            &XER::VideoEditor::Ui::MediaWidgetsSmoke);
        XER::VideoEditor::Smoke::RegisterCheck(
            "ui2_shell",
            &XER::VideoEditor::Ui2::Ui2ShellSmoke);
#endif
        for (const std::string& name :
             XER::VideoEditor::Smoke::CheckNames()) {
            std::cout << name << '\n';
        }
        return 0;
    }

    if (!arg.empty() && arg.front() != '-') {
#if defined(_WIN32)
        return UseUi2Shell() ? RunGui2(arg) : RunGui(arg);
#else
        std::cerr << "Error: desktop editor is Windows-only\n";
        return 1;
#endif
    }

    std::cerr << "Error: unknown argument '" << arg << "'\n";
    PrintUsage();
    return 1;
}
