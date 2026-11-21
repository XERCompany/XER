// main.cc — IDEXEJSO.exe entry point.
//
//   IDEXEJSO [project_dir]   open the IDE on an extension source dir
//   IDEXEJSO --selftest      headless scaffold+build+list check (CI)
//   IDEXEJSO --help          usage
//
// The window/app shell reuses the same framework the video editor
// boots (XER::UI::Core::Application + Window + AppTitleBar), matching
// the tools/preview pattern — just with IdeAppWidget as content.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ide_project.h"

#ifdef _WIN32
#include <windows.h>

#include <shellapi.h>

#include "ide_app.h"
#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/theme/theme.h"
#include "ui/widgets/app_title_bar.h"
#endif

namespace {

#ifdef _WIN32
std::string WideArgToUtf8(const wchar_t* w) {
    if (w == nullptr || *w == L'\0') return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.pop_back();
    return s;
}

// argv from char** main() is ANSI-encoded — re-parse the UTF-16
// command line so every path arrives as UTF-8 (same pattern as
// tools/preview and video_editor).
std::vector<std::string> CommandLineArgsUtf8() {
    std::vector<std::string> out;
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv == nullptr) return out;
    out.reserve(static_cast<std::size_t>(wargc));
    for (int i = 0; i < wargc; ++i) {
        out.push_back(WideArgToUtf8(wargv[i]));
    }
    LocalFree(wargv);
    return out;
}
#endif

void PrintUsage() {
    std::cout
        << "IDEXEJSO — VideoEditor extension IDE (.xexrj authoring)\n"
        << "Usage:\n"
        << "  IDEXEJSO [project_dir]   Open the IDE on an extension\n"
        << "                           source folder\n"
        << "  IDEXEJSO --selftest      Headless check: scaffold a scratch\n"
        << "                           extension, pack it via ztzmake.exe,\n"
        << "                           verify + list the archive (exit 0/1)\n"
        << "  IDEXEJSO --help          Show this help\n";
}

#ifdef _WIN32
int RunGui(const std::string& project_dir) {
    auto& app = XER::UI::Core::Application::Instance();
    app.SetAppName("Extension IDE - XER Creative Suite");
    if (!app.Initialize()) {
        std::cerr << "Error: failed to initialize XER UI\n";
        return 1;
    }
    auto window = std::make_shared<XER::UI::Core::Window>();
    if (!window->Create("Extension IDE - XER Creative Suite",
                        1280, 820)) {
        app.Shutdown();
        std::cerr << "Error: failed to create IDE window\n";
        return 1;
    }
    window->SetSizeLimits(860, 560, 0, 0);

    auto widget = std::make_shared<XER::IdeXejso::IdeAppWidget>(
        window->GetHWND(), project_dir);
    window->SetContent(widget);
    window->SetTheme(XER::UI::Theme::Variant::Dark);
    window->SetCustomTitleBar(true, 38.0f);
    window->SetTitleBarHitTest([widget](XER::UI::Layout::Point p) {
        auto* tb = widget->TitleBar();
        return tb != nullptr && tb->IsInteractiveAt(p);
    });

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
    widget->SetOnQuit([window] { window->Close(); });

    std::function<void(bool)> apply_theme =
        [window, tb, &apply_theme](bool dark) {
            window->SetTheme(dark ? XER::UI::Theme::Variant::Dark
                                  : XER::UI::Theme::Variant::Light);
            tb->SetThemeToggle(dark, apply_theme);
        };
    tb->SetThemeToggle(true, apply_theme);

    const auto sync_maximize = [window, tb] {
        tb->SetMaximized(window->IsWindowMaximized());
    };
    window->SetOnResize([sync_maximize](int, int) { sync_maximize(); });
    window->SetFrameCallback(
        [widget, sync_maximize] {
            widget->TickTree(1.0 / 30.0);
            sync_maximize();
        },
        33);

    window->Show();
    const int rc = app.Run();
    app.Shutdown();
    return rc;
}
#endif

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    (void)argc;
    (void)argv;
    const std::vector<std::string> args = CommandLineArgsUtf8();
#else
    const std::vector<std::string> args(argv, argv + argc);
#endif

    if (args.size() >= 2) {
        const std::string& a1 = args[1];
        if (a1 == "--help" || a1 == "-h" || a1 == "/?") {
            PrintUsage();
            return 0;
        }
        if (a1 == "--selftest") {
            return XER::IdeXejso::RunSelftest(
                [](const std::string& line) {
                    std::cout << line << '\n';
                });
        }
#ifdef _WIN32
        return RunGui(a1);   // treat first arg as the project dir
#else
        std::cerr << "Error: GUI is Windows-only\n";
        return 1;
#endif
    }

#ifdef _WIN32
    return RunGui("");
#else
    PrintUsage();
    return 1;
#endif
}
