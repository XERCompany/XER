#include "application.h"
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

#include "render_host.h"

namespace XER::UI::Core {

// Private implementation (PIMPL pattern)
class Application::Impl {
public:
    Impl() : initialized_(false), running_(false), app_name_("XER Application") {}

    bool Initialize() {
        if (initialized_) {
            return true;
        }

        // Per-monitor v2 awareness must be set before any window is created;
        // Window already tracks WM_DPICHANGED and scales input/layout by
        // 96/dpi, so without this the app is bitmap-scaled (blurry) and the
        // DPI paths never run. Fall back to system DPI awareness on older
        // Windows where the v2 context is unavailable.
        if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            SetProcessDPIAware();
        }

        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS;

        SetLastError(ERROR_SUCCESS);
        if (!InitCommonControlsEx(&icc)) {
            const DWORD error = GetLastError();

            fprintf(stderr,
                    "[XER UI] InitCommonControlsEx failed; "
                    "GetLastError=%lu\n",
                    static_cast<unsigned long>(error));
            return false;
        }

        // ABOVE_NORMAL keeps the UI responsive under load without starving
        // other processes the way HIGH_PRIORITY_CLASS would.
        if (!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
            const DWORD error = GetLastError();
            fprintf(stderr,
                    "[XER UI] SetPriorityClass failed; "
                    "GetLastError=%lu\n",
                    static_cast<unsigned long>(error)); 
        }

        // Pre-spawn the remote render worker pool: several hidden
        // xer_engine_ui_render.exe processes wait on their channels so
        // the first window checkouts instantly. Worker count:
        // SetRenderWorkerCount(n) before Initialize, or
        // XER_UI_RENDER_WORKERS env ("0" disables process rendering,
        // "auto" → one per 4 logical cores, capped at 8).
        int workers = render_worker_count_;
        if (const char* env = std::getenv("XER_UI_RENDER_WORKERS")) {
            if (_stricmp(env, "auto") == 0) {
                workers = static_cast<int>(
                    std::thread::hardware_concurrency() / 4);
                if (workers < 1) workers = 1;
                if (workers > 8) workers = 8;
            } else {
                workers = std::atoi(env);
            }
        }
        if (workers > 0) {
            std::string err;
            if (!UiRenderHost::Instance().EnsureWorkers(workers, &err)) {
                fprintf(stderr,
                        "[XER UI] render workers not spawned: %s\n",
                        err.c_str());
                // Not fatal: CreateRemoteRenderer retries on demand.
            }
        }

        initialized_ = true;
        return true;
    }

    int Run() {
        if (!initialized_) {
            throw std::runtime_error("Application not initialized");
        }

        running_ = true;

        // Main message loop. GetMessage returns -1 on error, which is truthy
        // as a BOOL — compare explicitly so an error can't spin the loop.
        MSG msg = {};
        for (;;) {
            const INT result = GetMessage(&msg, nullptr, 0, 0);
            if (result <= 0) {
                break; // 0 = WM_QUIT, -1 = error
            }
            if (!running_) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        return static_cast<int>(msg.wParam);
    }

    void Shutdown() {
        if (!initialized_) {
            return;
        }

        running_ = false;
        PostQuitMessage(0);

        // Tear down render workers cleanly (sends kShutdown on every
        // channel, then terminates stragglers).
        UiRenderHost::Instance().ShutdownAll();

        initialized_ = false;
    }

    void SetRenderWorkerCount(int count) { render_worker_count_ = count; }
    int RenderWorkerCount() const {
        return UiRenderHost::Instance().WorkerCount();
    }
    int WarmRenderWorkerCount() const {
        return UiRenderHost::Instance().WarmWorkerCount();
    }

    void SetAppName(const std::string& name) {
        app_name_ = name;
    }

    std::string GetAppName() const {
        return app_name_;
    }

    bool IsInitialized() const {
        return initialized_;
    }

    bool IsRunning() const {
        return running_;
    }

private:
    bool initialized_;
    bool running_;
    std::string app_name_;
    int render_worker_count_ = 2;  // pre-spawned render processes
};

// Application implementation
Application::Application() : impl_(std::make_unique<Impl>()) {}

Application::~Application() = default;

Application& Application::Instance() {
    static Application instance;
    return instance;
}

bool Application::Initialize() {
    return impl_->Initialize();
}

int Application::Run() {
    return impl_->Run();
}

void Application::Shutdown() {
    impl_->Shutdown();
}

void Application::SetAppName(const std::string& name) {
    impl_->SetAppName(name);
}

std::string Application::GetAppName() const {
    return impl_->GetAppName();
}

void Application::SetRenderWorkerCount(int count) {
    impl_->SetRenderWorkerCount(count);
}

int Application::RenderWorkerCount() const {
    return impl_->RenderWorkerCount();
}

int Application::WarmRenderWorkerCount() const {
    return impl_->WarmRenderWorkerCount();
}

bool Application::IsInitialized() const {
    return impl_->IsInitialized();
}

bool Application::IsRunning() const {
    return impl_->IsRunning();
}

} // namespace XER::UI::Core
