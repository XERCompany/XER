// xer_browser_shell.cc — entry point of the XERBrowserShell exe.
//
// Role 1 (primary): CEF browser-subprocess replacement. Host apps set
// settings.browser_subprocess_path to this exe (cef_runtime.cc prefers
// it over stock bootstrap.exe when deployed alongside); every
// --type=renderer/gpu/utility invocation lands in CefExecuteProcess
// below, whose app injects the `xer` src/ API object into each page.
//
// Role 2: standalone browser — `xer_browser_shell.exe <url>` opens a
// plain window; handy for testing the xer.* command surface without
// launching the video editor.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#if defined(XER_HAS_CEF)
#include <filesystem>
#include <string>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/wrapper/cef_closure_task.h"

#include "shell_commands.h"
#include "shell_render.h"

namespace {

// Minimal browser-process client for standalone mode: lifespan (quit
// when the last browser closes), title propagation, and the shell
// command dispatcher so xer.call works in standalone pages too.
class XERShellClient final : public CefClient,
                             public CefDisplayHandler,
                             public CefLifeSpanHandler {
public:
    explicit XERShellClient(HWND hwnd) : hwnd_(hwnd) {}

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
        return this;
    }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    void OnTitleChange(CefRefPtr<CefBrowser>,
                       const CefString& title) override {
        SetWindowTextW(hwnd_, title.ToWString().c_str());
    }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        browser_ = browser;
    }
    void OnBeforeClose(CefRefPtr<CefBrowser>) override {
        browser_ = nullptr;
        CefQuitMessageLoop();
    }
    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        CefProcessId,
        CefRefPtr<CefProcessMessage> message) override {
        return XER::Cef::HandleShellMessage(browser, frame, message);
    }

    CefRefPtr<CefBrowser> Browser() const { return browser_; }

private:
    HWND hwnd_;
    CefRefPtr<CefBrowser> browser_;

    XERShellClient(const XERShellClient&) = delete;
    XERShellClient& operator=(const XERShellClient&) = delete;

    IMPLEMENT_REFCOUNTING(XERShellClient);
};

XERShellClient* g_client = nullptr;   // set at window creation

LRESULT CALLBACK ShellWndProc(HWND hwnd, UINT msg, WPARAM wp,
                              LPARAM lp) {
    if (msg == WM_SIZE && g_client != nullptr &&
        g_client->Browser()) {
        const HWND child =
            g_client->Browser()->GetHost()->GetWindowHandle();
        if (child != nullptr) {
            MoveWindow(child, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        }
        return 0;
    }
    if (msg == WM_CLOSE && g_client != nullptr &&
        g_client->Browser()) {
        g_client->Browser()->GetHost()->CloseBrowser(false);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::wstring ExePath() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

std::string UrlFromCommandLine() {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string url;
    if (wargv != nullptr) {
        for (int i = 1; i < argc; ++i) {
            if (wargv[i][0] == L'-') continue;   // skip CEF flags
            char buf[2048]{};
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, buf,
                                sizeof buf, nullptr, nullptr);
            url = buf;
        }
        LocalFree(wargv);
    }
    return url.empty() ? "about:blank" : url;
}

}  // namespace

int RunShell(HINSTANCE instance) {
    CefMainArgs main_args(instance);
    CefRefPtr<CefApp> app = XER::Cef::CreateBrowserShellApp();

    // Subprocess role (--type=renderer/gpu/utility): never returns.
    const int code =
        CefExecuteProcess(main_args, app.get(), nullptr);
    if (code >= 0) {
        return code;
    }

    // Browser process — standalone window mode. Subprocesses respawn
    // this same exe so the renderer-side xer object is injected too.
    CefSettings settings;
    settings.no_sandbox = true;
    CefString(&settings.browser_subprocess_path) =
        CefString(ExePath());
    const std::string dir =
        std::filesystem::path(ExePath()).parent_path().string();
    CefString(&settings.resources_dir_path) = CefString(dir);
    CefString(&settings.locales_dir_path) =
        CefString(dir + "\\locales");
    settings.log_severity = LOGSEVERITY_WARNING;
    if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = ShellWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"XERBrowserShell";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"XER Browser Shell",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
        CW_USEDEFAULT, 1200, 800, nullptr, nullptr, instance,
        nullptr);
    if (hwnd == nullptr) {
        CefShutdown();
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);

    CefRefPtr<XERShellClient> client = new XERShellClient(hwnd);
    g_client = client.get();

    CefWindowInfo info;
    RECT rc;
    GetClientRect(hwnd, &rc);
    info.SetAsChild(hwnd, CefRect(0, 0, rc.right, rc.bottom));
    CefBrowserSettings bs;
    CefBrowserHost::CreateBrowser(info, client,
                                  CefString(UrlFromCommandLine()), bs,
                                  nullptr, nullptr);

    CefRunMessageLoop();
    g_client = nullptr;
    CefShutdown();
    return 0;
}

#else   // !XER_HAS_CEF

int RunShell(HINSTANCE) {
    MessageBoxW(nullptr,
                L"XERBrowserShell was built without CEF support "
                L"(binary distribution missing at configure time).",
                L"XER Browser Shell", MB_ICONWARNING | MB_OK);
    return 1;
}

#endif  // XER_HAS_CEF

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    return RunShell(instance);
}

#endif  // _WIN32
