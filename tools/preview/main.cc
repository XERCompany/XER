// preview.exe — Image viewer/editor + info tool using XER image API
//
// Usage:
//   preview.exe <image_file>              Open GUI viewer (scans folder,
//                                         arrows/PgUp/PgDn navigate,
//                                         wheel zoom, drag pan, crop,
//                                         rotate/flip, brightness/contrast,
//                                         save / export PNG)
//   preview.exe --info <image_file>       Print format/dimension/metadata dump
//   preview.exe --register-context-menu   Add "Preview with XER3" to the
//                                         Windows Explorer context menu
//                                         (HKCU, no admin required)
//   preview.exe --unregister-context-menu Remove the context menu entries
//
// Supported formats: JPEG, PNG, WEBP, TIFF, GIF, BMP, PSD, EXR, HEIF, SVG

#include <functional>
#include <iostream>
#include <string>
#include <memory>
#include <vector>

#include <absl/strings/str_cat.h>

#include "image/io/image_loader.h"
#include "image/core/image_buffer.h"
#include "image/core/image_descriptor.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_styles.h"
#include "helper/string_utils.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>

#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/theme/theme.h"
#include "ui/widgets/app_title_bar.h"
#include "photo_browser.h"
#include "photo_viewer_widget.h"
#endif

namespace {
namespace Str = XER::Helper::String;

#if defined(_WIN32)
#pragma comment(lib, "shell32.lib")

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

// argv from char** main() is ANSI-encoded and mangles non-ASCII paths
// (e.g. Cyrillic names passed by the Explorer context menu). Re-parse the
// original UTF-16 command line so every argument arrives as UTF-8.
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
              << "  preview.exe <image_file>               Open GUI photo viewer\n"
              << "  preview.exe --info <image_file>        Print image information\n"
              << "  preview.exe --register-context-menu    Register Explorer context menu\n"
              << "  preview.exe --unregister-context-menu  Remove context menu entries\n"
              << "\nSupported formats: JPEG, PNG, WEBP, TIFF, GIF, BMP, PSD, EXR, HEIF, SVG\n";
}

std::string PixelFormatToString(image::PixelFormat fmt) {
    switch (fmt) {
        case image::PixelFormat::RGBA8: return "RGBA8";
        case image::PixelFormat::BGRA8: return "BGRA8";
        case image::PixelFormat::RGB8: return "RGB8";
        case image::PixelFormat::BGR8: return "BGR8";
        case image::PixelFormat::Gray8: return "Gray8";
        case image::PixelFormat::Gray16: return "Gray16";
        case image::PixelFormat::RGBA16: return "RGBA16";
        case image::PixelFormat::RGBA32F: return "RGBA32F (HDR)";
        case image::PixelFormat::YUV420P: return "YUV420P";
        default: return "Unknown";
    }
}

std::string ColorSpaceToString(image::ColorSpace cs) {
    switch (cs) {
        case image::ColorSpace::sRGB: return "sRGB";
        case image::ColorSpace::LinearRGB: return "Linear RGB";
        case image::ColorSpace::AdobeRGB: return "Adobe RGB";
        case image::ColorSpace::ACES_CG: return "ACES CG";
        case image::ColorSpace::Rec709: return "Rec. 709";
        case image::ColorSpace::Rec2020: return "Rec. 2020";
        case image::ColorSpace::P3_D65: return "Display P3";
        case image::ColorSpace::Gray: return "Gray";
        default: return "Unknown";
    }
}

std::string FormatSize(int width, int height) {
    const double megapixels = (width * height) / 1000000.0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%dx%d (%.2f MP)", width, height, megapixels);
    return std::string(buf);
}

std::string FormatBytes(size_t bytes) {
    const double mb = bytes / (1024.0 * 1024.0);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f MB", mb);
    return std::string(buf);
}

int RunInfoMode(const std::string& input_path) {
    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kCyan),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "Image Info: ", input_path,
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    image::ImageLoader loader;
    image::ImageDescriptor desc;
    auto buffer = loader.Load(input_path, desc);

    if (!buffer || !buffer->IsValid()) {
        std::cerr << "Error: Failed to load image\n";
        return 1;
    }

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
            "Format: ", desc.format_name, "\n",
            "Source: ", desc.source_path
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Dimensions ===",
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Size: ", FormatSize(desc.width, desc.height), "\n",
            "Width: ", desc.width, " px\n",
            "Height: ", desc.height, " px\n",
            "DPI: ", desc.dpi_x, " x ", desc.dpi_y
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Color Information ===",
            XER::Flux::Terminal::ResetStyleSequence()
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Pixel Format: ", PixelFormatToString(desc.format), "\n",
            "Color Space: ", ColorSpaceToString(desc.color_space), "\n",
            "Channels: ", desc.channels, "\n",
            "Bit Depth: ", desc.depth, " bits\n",
            "Has Alpha: ", desc.has_alpha ? "Yes" : "No", "\n",
            "HDR: ", desc.is_hdr ? "Yes" : "No"
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
            "Buffer Size: ", FormatBytes(buffer->DataSize()), "\n",
            "Buffer Format: ", PixelFormatToString(buffer->Format()), "\n",
            "Buffer Color Space: ", ColorSpaceToString(buffer->GetColorSpace())
        )
    );

    if (desc.page_count > 1) {
        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat(
                XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
                XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
                "\n=== Multi-Page Information ===",
                XER::Flux::Terminal::ResetStyleSequence()
            )
        );

        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat("Pages: ", desc.page_count)
        );
    }

    if (!desc.metadata.empty()) {
        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat(
                XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
                XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
                "\n=== Metadata ===",
                XER::Flux::Terminal::ResetStyleSequence()
            )
        );

        for (const auto& [key, value] : desc.metadata) {
            XER::Flux::Terminal::WriteLine(
                XER::Flux::Terminal::OutputStream::kStdout,
                absl::StrCat(key, ": ", value)
            );
        }
    }

    return 0;
}

#if defined(_WIN32)

/// Extensions registered under SystemFileAssociations — works for files that
/// have no per-user association override and needs no admin rights.
const wchar_t* kContextMenuExts[] = {
    L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".webp",
    L".tif", L".tiff", L".heic", L".heif", L".avif",
};

constexpr const wchar_t* kVerbKeyTail = L"\\shell\\XER3Preview";
constexpr const wchar_t* kVerbName = L"Preview with XER3";

std::wstring ExePathW() {
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return n > 0 ? std::wstring(buf, n) : std::wstring();
}

int SetKeyString(HKEY root, const std::wstring& subkey,
                 const std::wstring& value_name, const std::wstring& data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return static_cast<int>(GetLastError());
    }
    const LSTATUS st = RegSetValueExW(
        key, value_name.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(data.c_str()),
        static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return st == ERROR_SUCCESS ? 0 : static_cast<int>(st);
}

int RunContextMenu(bool register_menu) {
    const std::wstring exe = ExePathW();
    if (exe.empty()) {
        std::cerr << "Error: cannot determine preview.exe path\n";
        return 1;
    }

    int failures = 0;
    for (const wchar_t* ext : kContextMenuExts) {
        char narrow[16] = {};
        size_t converted = 0;
        wcstombs_s(&converted, narrow, sizeof(narrow), ext, _TRUNCATE);

        const std::wstring base =
            std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext +
            kVerbKeyTail;
        if (register_menu) {
            const int e1 = SetKeyString(HKEY_CURRENT_USER, base, L"",
                                        kVerbName);
            const int e2 = SetKeyString(HKEY_CURRENT_USER, base + L"\\Icon", L"",
                                        exe);
            const int e3 = SetKeyString(
                HKEY_CURRENT_USER, base + L"\\command", L"",
                L"\"" + exe + L"\" \"%1\"");
            const bool ok = (e1 | e2 | e3) == 0;
            std::cout << (ok ? "  + " : "  ! ") << narrow;
            if (!ok) {
                ++failures;
                std::cout << " (error " << (e1 ? e1 : (e2 ? e2 : e3)) << ")";
            }
            std::cout << "\n";
        } else {
            const LSTATUS st = RegDeleteTreeW(
                HKEY_CURRENT_USER,
                (std::wstring(L"Software\\Classes\\SystemFileAssociations\\") +
                 ext + kVerbKeyTail).c_str());
            std::cout << (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND
                              ? "  - removed "
                              : "  ! failed  ")
                      << narrow;
            if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) ++failures;
            std::cout << "\n";
        }
    }

    std::cout << (register_menu
                      ? "Context menu 'Preview with XER3' registered under HKCU.\n"
                      : "Context menu entries removed.\n");
    return failures == 0 ? 0 : 1;
}

int RunGui(const std::string& image_path) {
    auto browser = std::make_unique<preview::PhotoBrowser>();
    if (!browser->Open(image_path)) {
        std::cerr << "Error: " << browser->StatusMessage() << "\n";
        return 1;
    }

    auto& ui_app = XER::UI::Core::Application::Instance();
    if (!ui_app.Initialize()) {
        std::cerr << "Error: UI initialization failed\n";
        return 1;
    }

    auto window = std::make_shared<XER::UI::Core::Window>();
    if (!window->Create("XER Preview", 1280, 800)) {
        std::cerr << "Error: window creation failed\n";
        ui_app.Shutdown();
        return 1;
    }
    window->SetSizeLimits(860, 560, 0, 0);

    auto widget = std::make_shared<preview::PhotoViewerWidget>(browser.get(),
                                                             window.get());
    window->SetContent(widget);

    // Frameless IDE-style chrome: AppTitleBar supplies min/max/close and the
    // File/Info menu bar; empty bar areas act as native drag regions.
    window->SetCustomTitleBar(true, 38.0f);
    window->SetTitleBarHitTest([widget](XER::UI::Layout::Point p) {
        auto* tb = widget->TitleBar();
        return tb != nullptr && tb->IsInteractiveAt(p);
    });
    window->SetTheme(XER::UI::Theme::Variant::Dark);

    auto* tb = widget->TitleBar();
    using WindowCommand = XER::UI::Widgets::AppTitleBar::WindowCommand;
    tb->SetOnWindowCommand([window](WindowCommand cmd) {
        switch (cmd) {
            case WindowCommand::Minimize: window->Minimize(); break;
            case WindowCommand::ToggleMaximize: window->ToggleMaximize(); break;
            case WindowCommand::Close: window->Close(); break;
        }
    });
    widget->SetOnQuitRequest([window] { window->Close(); });

    std::function<void(bool)> apply_theme = [window, tb, &apply_theme](bool dark) {
        window->SetTheme(dark ? XER::UI::Theme::Variant::Dark
                              : XER::UI::Theme::Variant::Light);
        tb->SetThemeToggle(dark, apply_theme);
    };
    tb->SetThemeToggle(true, apply_theme);

    auto sync_maximize = [window, tb]() {
        tb->SetMaximized(window->IsWindowMaximized());
    };
    window->SetOnResize([sync_maximize](int, int) { sync_maximize(); });

    window->SetFrameCallback(
        [widget, sync_maximize]() {
            widget->TickTree(0.016);
            sync_maximize();
        },
        66);

    window->Show();

    const int rc = ui_app.Run();
    ui_app.Shutdown();
    return rc;
}

#endif  // _WIN32

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    // Error/status output carries UTF-8 paths; make the console display them.
    SetConsoleOutputCP(CP_UTF8);
    (void)argc;
    (void)argv;
    const std::vector<std::string> args = CommandLineArgsUtf8();
#else
    std::vector<std::string> args(argv, argv + argc);
#endif
    if (args.size() < 2) {
        PrintUsage();
        return 1;
    }

    const std::string& arg = args[1];

    if (arg == "--help" || arg == "-h" || arg == "/?") {
        PrintUsage();
        return 0;
    }

    if (arg == "--info") {
        if (args.size() < 3) {
            std::cerr << "Error: --info requires an image path\n";
            return 1;
        }
        return RunInfoMode(args[2]);
    }

#if defined(_WIN32)
    if (arg == "--register-context-menu") {
        return RunContextMenu(true);
    }
    if (arg == "--unregister-context-menu") {
        return RunContextMenu(false);
    }

    // Default: GUI photo viewer. Paths arrive as UTF-8 (see above).
    return RunGui(args[1]);
#else
    std::cerr << "GUI viewer requires Windows. Use --info for text output.\n";
    return 1;
#endif
}
