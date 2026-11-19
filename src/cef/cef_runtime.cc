// cef_runtime.cc — see cef_runtime.h. The whole TU compiles to inert
// stubs without XER_HAS_CEF so non-CEF builds keep the same interface.
#include "cef_runtime.h"
#include "cef_ext_scheme.h"

#include <atomic>
#include <filesystem>
#include <mutex>

#if defined(XER_HAS_CEF)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "include/cef_app.h"
#include "include/cef_client.h"
#endif

namespace XER::Cef {

#if defined(XER_HAS_CEF)
namespace {

// Minimal CefApp — no render-process callbacks needed for browser-only
// embedding; browser-process handler is the default implementation.
class XERCefApp final : public CefApp, public CefBrowserProcessHandler {
public:
    explicit XERCefApp(std::vector<std::string> switches)
        : switches_(std::move(switches)) {}
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler()
        override {
        return this;
    }
    void OnBeforeCommandLineProcessing(
        const CefString&, CefRefPtr<CefCommandLine> cl) override {
        for (const auto& s : switches_) {
            const auto eq = s.find('=');
            if (eq == std::string::npos) {
                cl->AppendSwitch(s);
            } else {
                cl->AppendSwitchWithValue(s.substr(0, eq),
                                          s.substr(eq + 1));
            }
        }
    }
    void OnContextInitialized() override {
        // xer-ext:// serves .xexrj extension pages from verified TZ
        // archives — the factory is registry-driven so extensions can
        // be registered/unregistered at runtime.
        RegisterExtSchemeHandler();
    }

    XERCefApp(const XERCefApp&) = delete;
    XERCefApp& operator=(const XERCefApp&) = delete;

private:
    std::vector<std::string> switches_;
    IMPLEMENT_REFCOUNTING(XERCefApp);
};

std::mutex g_mutex;
bool g_initialized = false;
std::string g_last_error;
CefRefPtr<XERCefApp> g_app;

// Absolute path of the running executable's directory — CEF resources
// are deployed beside the exe by the build's deploy step.
std::string ExeDir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buf).parent_path().string();
}

CefString Utf8(const std::string& s) { return CefString(s); }

}  // namespace

int RunSubprocessIfAny(int argc, char** argv) {
    CefMainArgs args(GetModuleHandle(nullptr));
    // No app for subprocess types: they must not run browser-process
    // callbacks; CefExecuteProcess returns -1 only for OUR process.
    return CefExecuteProcess(args, nullptr, nullptr);
}

bool Initialize(const RuntimeOptions& options, std::string* error_out) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_initialized) {
        return true;
    }
    if (!g_last_error.empty()) {
        if (error_out != nullptr) {
            *error_out = g_last_error;
        }
        return false;   // sticky failure — no retry storms
    }

    const std::string exe_dir = ExeDir();
    if (exe_dir.empty()) {
        g_last_error = "CEF: cannot resolve executable directory";
        if (error_out != nullptr) *error_out = g_last_error;
        return false;
    }
    // Fail fast with an actionable message when the deploy step did not
    // place libcef.dll beside the binary.
    const std::filesystem::path lib =
        std::filesystem::path(exe_dir) / "libcef.dll";
    if (!std::filesystem::exists(lib)) {
        g_last_error = "CEF: libcef.dll not found next to the executable "
                       "(run the CEF deploy step)";
        if (error_out != nullptr) *error_out = g_last_error;
        return false;
    }

    CefSettings settings;
    settings.no_sandbox = true;   // we do not build libcef_dll_sandbox
    // CEF runs its own UI thread — our Win32 loop stays untouched.
    settings.multi_threaded_message_loop = true;
    settings.windowless_rendering_enabled = options.headless;
    // Note: CEF also echoes ERROR+ to stderr on console builds; only
    // FATAL/DISABLE keeps the console clean (see cef_log_severity_t).
    settings.log_severity =
        options.log_severity >= 0
            ? static_cast<cef_log_severity_t>(options.log_severity)
            : LOGSEVERITY_WARNING;
    if (!options.log_file.empty()) {
        CefString(&settings.log_file) = Utf8(options.log_file);
    }
    // Newer CEF ships bootstrap.exe as the subprocess launcher; it must
    // sit beside the executable (deploy step does this). Resource files
    // (*.pak, icudtl.dat, locales/) are also deployed beside the exe.
    const std::string res_dir = options.resources_dir.empty()
                                    ? exe_dir
                                    : options.resources_dir;
    // Prefer our own subprocess shell — it injects the `xer` src/ API
    // object into every page. Stock bootstrap.exe is the fallback when
    // the shell was not built/deployed.
    const std::string shell = exe_dir + "\\xer_browser_shell.exe";
    CefString(&settings.browser_subprocess_path) = Utf8(
        options.subprocess_path.empty()
            ? (std::filesystem::exists(shell)
                   ? shell
                   : exe_dir + "\\bootstrap.exe")
            : options.subprocess_path);
    CefString(&settings.resources_dir_path) = Utf8(res_dir);
    CefString(&settings.locales_dir_path) =
        Utf8(options.locales_dir.empty() ? res_dir + "\\locales"
                                         : options.locales_dir);
    if (!options.cache_dir.empty()) {
        CefString(&settings.root_cache_path) = Utf8(options.cache_dir);
        CefString(&settings.cache_path) = Utf8(options.cache_dir);
    }
    if (options.enable_remote_debug && options.remote_debug_port > 0) {
        settings.remote_debugging_port = options.remote_debug_port;
    }

    g_app = new XERCefApp(options.command_line_switches);
    CefMainArgs args(GetModuleHandle(nullptr));
    if (!CefInitialize(args, settings, g_app.get(), nullptr)) {
        g_last_error = "CEF: CefInitialize failed (resources or version "
                       "mismatch beside the executable)";
        if (error_out != nullptr) *error_out = g_last_error;
        g_app = nullptr;
        return false;
    }
    g_initialized = true;
    return true;
}

bool IsInitialized() { return g_initialized; }

void Shutdown() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!g_initialized) {
            return;
        }
        g_initialized = false;
    }
    CefShutdown();
    g_app = nullptr;
}

bool Available() { return true; }

#else   // !XER_HAS_CEF — inert stubs

int RunSubprocessIfAny(int, char**) { return -1; }

bool Initialize(const RuntimeOptions&, std::string* error_out) {
    if (error_out != nullptr) {
        *error_out = "CEF support is not compiled in (binary "
                     "distribution missing at configure time)";
    }
    return false;
}

bool IsInitialized() { return false; }
void Shutdown() {}
bool Available() { return false; }

#endif  // XER_HAS_CEF

}  // namespace XER::Cef
