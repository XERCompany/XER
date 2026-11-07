#include "app_ui.h"

#include "ui/core/window.h"
#include "ui/core/application.h"
#include "ui/widgets/audio_player_widget.h"

namespace App {

class AppUI::Impl {
public:
    Impl()
        : ui_initialized_(false)
        , window_created_(false) {
    }

    ~Impl() {
        if (ui_initialized_) {
            XER::UI::Core::Application::Instance().Shutdown();
        }
    }

    bool Initialize() {
        if (ui_initialized_) {
            return true;
        }

        auto& ui_app = XER::UI::Core::Application::Instance();
        if (!ui_app.Initialize()) {
            return false;
        }

        ui_initialized_ = true;
        return true;
    }

    void Shutdown() {
        if (ui_initialized_) {
            XER::UI::Core::Application::Instance().Shutdown();
            ui_initialized_ = false;
        }
    }

    bool CreateMainWindow(const std::string& title, int width, int height) {
        if (!ui_initialized_) {
            return false;
        }

        window_ = std::make_shared<XER::UI::Core::Window>();
        if (!window_->Create(title, width, height)) {
            window_.reset();
            return false;
        }

        window_created_ = true;
        return true;
    }

    void ShowWindow() {
        if (window_) {
            window_->Show();
        }
    }

    void HideWindow() {
        if (window_) {
            window_->Hide();
        }
    }

    void CloseWindow() {
        if (window_) {
            window_->Close();
        }
    }

    void SetupAudioPlayerWidget(AudioController* controller) {
        if (!window_ || !controller) {
            return;
        }

        audio_widget_ = std::make_shared<XER::UI::Widgets::AudioPlayerWidget>(controller);
        window_->SetContent(audio_widget_);

        // Set up close callback to stop audio
        window_->SetOnClose([controller]() {
            controller->Stop();
        });

        SetTimer(window_->GetHWND(), 1, 100, [](HWND hwnd, UINT, UINT_PTR, DWORD) {
            InvalidateRect(hwnd, nullptr, FALSE);
        });
    }

    int Run() {
        if (!ui_initialized_) {
            return 1;
        }

        return XER::UI::Core::Application::Instance().Run();
    }

    bool IsWindowVisible() const {
        return window_ && window_->IsVisible();
    }

private:
    bool ui_initialized_;
    bool window_created_;
    std::shared_ptr<XER::UI::Core::Window> window_;
    std::shared_ptr<XER::UI::Widgets::AudioPlayerWidget> audio_widget_;
};

AppUI::AppUI()
    : impl_(std::make_unique<Impl>())
    , initialized_(false) {
}

AppUI::~AppUI() {
    if (initialized_) {
        Shutdown();
    }
}

bool AppUI::Initialize() {
    if (initialized_) {
        return true;
    }

    if (!impl_->Initialize()) {
        return false;
    }

    initialized_ = true;
    return true;
}

void AppUI::Shutdown() {
    if (initialized_) {
        impl_->Shutdown();
        initialized_ = false;
    }
}

bool AppUI::CreateMainWindow(const std::string& title, int width, int height) {
    if (!initialized_) {
        return false;
    }

    return impl_->CreateMainWindow(title, width, height);
}

void AppUI::ShowWindow() {
    impl_->ShowWindow();
}

void AppUI::HideWindow() {
    impl_->HideWindow();
}

void AppUI::CloseWindow() {
    impl_->CloseWindow();
}

void AppUI::SetupAudioPlayerWidget(AudioController* controller) {
    impl_->SetupAudioPlayerWidget(controller);
}

int AppUI::Run() {
    return impl_->Run();
}

bool AppUI::IsWindowVisible() const {
    return impl_->IsWindowVisible();
}

}  // namespace App
