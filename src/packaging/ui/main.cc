// xer_packager — app bundler UI + CLI on src/packaging.
//
//   xer_packager                                   GUI (CPU raster only)
//   xer_packager pack --exe app.exe --out dir
//                    [--name N] [--version V] [--icon i.ico]
//                    [--add file] [--search dir] [--zip]
//   xer_packager scan --exe app.exe [--search dir]
//   xer_packager screenshot --out shot.png [--exe app.exe] [--size WxH]
//
// The GUI is strictly software-rendered: it pins RendererBackend::
// kSoftware (GDI-presented BGRA rasterizer) — never the D2D/D3D11/
// Vulkan providers — and disables the render-worker pool, so no GPU or
// second process is involved.

#include "packaging/dependency_scanner.h"
#include "packaging/packager.h"
#include "packaging/ui/packager_widget.h"
#include "packaging/ui/png_writer.h"

#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/rendering/renderer_provider.h"
#include "ui/rendering/windows/software_renderer.h"
#include "ui/theme/theme.h"
#include "ui/widgets/app_title_bar.h"
#include "ui/widgets/widget_tree.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace PK = XER::Packaging;

namespace {

void Usage(const char* argv0) {
    std::cerr
        << "usage:\n"
        << "  " << argv0 << "                 launch the GUI\n"
        << "  " << argv0
        << " pack --exe <app.exe> --out <dir> [opts]\n"
        << "  " << argv0 << " scan --exe <app.exe> [--search <dir>]\n"
        << "  " << argv0
        << " screenshot --out <file.png> [--exe <app.exe>] [--size WxH]\n"
        << "\npack options:\n"
        << "  --name <n>      package name (default: exe stem)\n"
        << "  --version <v>   version string (default 0.0.0)\n"
        << "  --icon <.ico>   icon file copied into the bundle\n"
        << "  --add <path>    extra file or directory (repeatable)\n"
        << "  --search <dir>  extra DLL search dir (repeatable; default:\n"
        << "                  exe dir + out/build/*/bin under the repo)\n"
        << "  --zip           also write <name>-<ver>-windows-x64.zip\n";
}

bool ParseWxH(const std::string& s, int* w, int* h) {
    const auto x = s.find('x');
    if (x == std::string::npos) return false;
    try {
        *w = std::stoi(s.substr(0, x));
        *h = std::stoi(s.substr(x + 1));
    } catch (...) {
        return false;
    }
    return *w > 0 && *h > 0;
}

// Shared flag/option parser for `pack` and `scan`.
struct CliSpec {
    PK::PackageSpec spec;
    bool has_exe = false;
    bool has_out = false;
};

// Returns false (with usage already printed) on a bad arg.
bool ParseSpecArgs(const std::vector<std::string>& args, std::size_t begin,
                   CliSpec* out, bool* zip_seen) {
    for (std::size_t i = begin; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto need = [&](const char* flag) -> const char* {
            if (i + 1 >= args.size()) {
                std::cerr << "error: " << flag << " requires a value\n";
                return nullptr;
            }
            return args[++i].c_str();
        };
        if (a == "--exe") {
            const char* v = need("--exe");
            if (v == nullptr) return false;
            out->spec.exe_path = fs::path(v);
            out->has_exe = true;
        } else if (a == "--out") {
            const char* v = need("--out");
            if (v == nullptr) return false;
            out->spec.output_dir = fs::path(v);
            out->has_out = true;
        } else if (a == "--name") {
            const char* v = need("--name");
            if (v == nullptr) return false;
            out->spec.name = v;
        } else if (a == "--version") {
            const char* v = need("--version");
            if (v == nullptr) return false;
            out->spec.version = v;
        } else if (a == "--icon") {
            const char* v = need("--icon");
            if (v == nullptr) return false;
            out->spec.icon_path = fs::path(v);
        } else if (a == "--add") {
            const char* v = need("--add");
            if (v == nullptr) return false;
            std::error_code ec;
            if (fs::is_directory(fs::path(v), ec))
                out->spec.extra_dirs.push_back(fs::path(v));
            else
                out->spec.extra_files.push_back(fs::path(v));
        } else if (a == "--search") {
            const char* v = need("--search");
            if (v == nullptr) return false;
            out->spec.search_dirs.push_back(fs::path(v));
        } else if (a == "--zip") {
            *zip_seen = true;
        } else {
            std::cerr << "error: unknown option '" << a << "'\n";
            return false;
        }
    }
    return true;
}

int RunPack(const std::vector<std::string>& args) {
    CliSpec cli;
    bool zip = false;
    if (!ParseSpecArgs(args, 2, &cli, &zip)) return 2;
    if (!cli.has_exe || !cli.has_out) {
        std::cerr << "error: pack requires --exe and --out\n";
        return 2;
    }
    cli.spec.format =
        zip ? PK::PackageFormat::Zip : PK::PackageFormat::Staging;

    PK::PackageResult result;
    std::string error;
    const bool ok = PK::Packager::Pack(
        cli.spec, &result, &error,
        [](float f, const std::string& step) {
            std::fprintf(stderr, "\r[%3.0f%%] %-60s", f * 100.0f,
                         step.c_str());
        },
        [](const std::string& line) { std::cerr << line << "\n"; });
    std::fprintf(stderr, "\n");
    if (!ok) {
        std::cerr << "pack failed: " << error << "\n";
        return 1;
    }
    std::cout << result.manifest_path.string() << "\n";
    if (!result.archive_path.empty())
        std::cout << result.archive_path.string() << "\n";
    return 0;
}

int RunScan(const std::vector<std::string>& args) {
    CliSpec cli;
    bool zip = false;
    if (!ParseSpecArgs(args, 2, &cli, &zip)) return 2;
    if (!cli.has_exe) {
        std::cerr << "error: scan requires --exe\n";
        return 2;
    }
    std::vector<fs::path> dirs = cli.spec.search_dirs;
    if (dirs.empty())
        dirs = PK::Packager::DefaultSearchDirs(cli.spec.exe_path);

    PK::DependencyScanResult scan;
    std::string error;
    if (!PK::ScanDependencies(cli.spec.exe_path, dirs, &scan, &error)) {
        std::cerr << "scan failed: " << error << "\n";
        return 1;
    }
    for (const auto& dep : scan.dlls) {
        std::cout << dep.path.filename().string() << "\t"
                  << (dep.origin == PK::DepOrigin::ExeDir ? "exe-dir"
                                                          : "search-dir")
                  << (dep.delay_loaded ? " (delay)" : "") << "\t"
                  << dep.path.string() << "\n";
    }
    std::cerr << "scan: " << scan.dlls.size() << " resolved, "
              << scan.unresolved.size() << " OS/unresolved\n";
    for (const auto& w : scan.warnings)
        std::cerr << "warn: " << w << "\n";
    return 0;
}

// Headless full-UI capture — the video_editor --screenshot --ui pattern:
// real widget tree laid out + painted into a headless SoftwareRenderer.
int RunScreenshot(const std::vector<std::string>& args) {
    std::string out_path;
    std::string exe;
    int w = 980, h = 680;
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto need = [&](const char* flag) -> const char* {
            if (i + 1 >= args.size()) {
                std::cerr << "error: " << flag << " requires a value\n";
                return nullptr;
            }
            return args[++i].c_str();
        };
        if (a == "--out") {
            const char* v = need("--out");
            if (v == nullptr) return 2;
            out_path = v;
        } else if (a == "--exe") {
            const char* v = need("--exe");
            if (v == nullptr) return 2;
            exe = v;
        } else if (a == "--size") {
            const char* v = need("--size");
            if (v == nullptr || !ParseWxH(v, &w, &h)) {
                std::cerr << "error: --size requires WxH\n";
                return 2;
            }
        } else {
            std::cerr << "error: unknown screenshot option '" << a
                      << "'\n";
            return 2;
        }
    }
    if (out_path.empty()) {
        std::cerr << "error: screenshot requires --out\n";
        return 2;
    }

#if defined(_WIN32)
    XER::UI::Widgets::WidgetTree tree;
    auto widget = std::make_shared<PK::Ui::PackagerWidget>(nullptr);
    tree.SetRoot(widget);
    if (!exe.empty()) {
        widget->SetExePath(exe);
        std::string scan_err;
        if (!widget->ScanNow(&scan_err))
            std::cerr << "screenshot: scan failed: " << scan_err
                      << "\n";
    }

    XER::UI::Theme::Provider theme;
    theme.SetVariant(XER::UI::Theme::Variant::Dark);
    const auto& tokens = theme.GetTokens();

    XER::UI::Rendering::Windows::SoftwareRenderer renderer(nullptr);
    renderer.SetDpi(96);  // 1 DIP == 1 px
    renderer.SetClearColor(tokens.background);
    const XER::UI::Layout::Rect bounds{
        0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};

    try {
        // Two passes: first warms lazy per-widget state, second draws
        // the settled tree — same as tools/video_editor --screenshot.
        for (int pass = 0; pass < 2; ++pass) {
            widget->TickTree(1.0 / 30.0);
            tree.Layout(bounds, tokens);
            tree.Render(renderer, tokens, nullptr);
        }
    } catch (const std::exception& e) {
        std::cerr << "render failed: " << e.what() << "\n";
        tree.SetRoot(nullptr);
        return 1;
    }

    const std::uint32_t* pixels = renderer.Pixels();
    std::string error;
    const bool saved =
        pixels != nullptr &&
        PK::Ui::WriteBgraPng(
            reinterpret_cast<const std::uint8_t*>(pixels),
            renderer.PixelWidth(), renderer.PixelHeight(), out_path,
            &error);
    tree.SetRoot(nullptr);
    widget.reset();
    if (!saved) {
        std::cerr << "screenshot failed: " << error << "\n";
        return 1;
    }
    std::cout << "screenshot: " << out_path << " " << w << "x" << h
              << " ok\n";
    return 0;
#else
    std::cerr << "screenshot is Windows-only\n";
    return 1;
#endif
}

int RunGui(const std::string& exe_prefill) {
#if defined(_WIN32)
    auto& app = XER::UI::Core::Application::Instance();
    app.SetAppName("xer_packager");
    // No process-separated render workers: this app is software-rendered
    // only — the CPU rasterizer runs in-process.
    app.SetRenderWorkerCount(0);
    if (!app.Initialize()) return 1;

    auto window = std::make_shared<XER::UI::Core::Window>();
    if (!window->Create("XER Packager", 980, 680)) return 1;

    // CPU render only: pin the software provider — never D2D/D3D11/
    // Vulkan, never ANGLE/Skia-GPU.
    window->SetRenderer(XER::UI::Rendering::CreateRenderer(
        XER::UI::Rendering::RendererBackend::kSoftware,
        window->GetHWND()));

    auto widget =
        std::make_shared<PK::Ui::PackagerWidget>(window->GetHWND());
    if (!exe_prefill.empty()) widget->SetExePath(exe_prefill);
    window->SetContent(widget);
    window->SetOnClose([] {});

    window->SetCustomTitleBar(true, 38.0f);
    window->SetTitleBarHitTest(
        [widget](XER::UI::Layout::Point p) {
            auto* tb = widget->TitleBar();
            return tb != nullptr && tb->IsInteractiveAt(p);
        });
    window->SetTheme(XER::UI::Theme::Variant::Dark);
    window->SetSizeLimits(760, 520, 0, 0);

    auto* tb = widget->TitleBar();
    using WindowCommand = XER::UI::Widgets::AppTitleBar::WindowCommand;
    tb->SetOnWindowCommand([window](WindowCommand cmd) {
        switch (cmd) {
            case WindowCommand::Minimize: window->Minimize(); break;
            case WindowCommand::ToggleMaximize:
                window->ToggleMaximize();
                break;
            case WindowCommand::Close: window->Close(); break;
        }
    });
    tb->SetThemeToggle(true, [window](bool dark) {
        window->SetTheme(dark ? XER::UI::Theme::Variant::Dark
                              : XER::UI::Theme::Variant::Light);
    });

    window->SetOnResize([window, tb](int, int) {
        tb->SetMaximized(window->IsWindowMaximized());
    });
    window->SetFrameCallback(
        [widget, window, tb]() {
            widget->TickTree(0.016);
            tb->SetMaximized(window->IsWindowMaximized());
        },
        33);

    window->Show();
    return app.Run();
#else
    std::cerr << "xer_packager GUI is Windows-only; use pack/scan\n";
    return 1;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv, argv + argc);
    if (argc <= 1) return RunGui("");

    const std::string& cmd = args[1];
    if (cmd == "pack") return RunPack(args);
    if (cmd == "scan") return RunScan(args);
    if (cmd == "screenshot") return RunScreenshot(args);
    if (cmd == "--gui") return RunGui(argc > 2 ? args[2] : "");
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        Usage(argv[0]);
        return 0;
    }
    // Convenience: a bare path to an .exe launches the GUI prefilled.
    std::error_code ec;
    if (fs::exists(args[1], ec)) return RunGui(args[1]);
    std::cerr << "error: unknown command '" << cmd << "'\n";
    Usage(argv[0]);
    return 2;
}
