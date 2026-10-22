#include "application.h"
#include <windows.h>
#include <stdexcept>

namespace XER::UI::Core {

// Private implementation (PIMPL pattern)
class Application::Impl {
public:
    Impl() : initialized_(false), running_(false), app_name_("XER Application") {}

    bool Initialize() {
        if (initialized_) {
            return true;
        }

        // Initialize WinAPI common controls
        INITCOMMONCONTROLSEX icc = {};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
        if (!InitCommonControlsEx(&icc)) {
            return false;
        }

        // Increase process priority for better UI responsiveness
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

        initialized_ = true;
        return true;
    }

    int Run() {
        if (!initialized_) {
            throw std::runtime_error("Application not initialized");
        }

        running_ = true;

        // Main message loop
        MSG msg = {};
        while (running_ && GetMessage(&msg, nullptr, 0, 0)) {
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

        initialized_ = false;
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

bool Application::IsInitialized() const {
    return impl_->IsInitialized();
}

bool Application::IsRunning() const {
    return impl_->IsRunning();
}

} // namespace XER::UI::Core
