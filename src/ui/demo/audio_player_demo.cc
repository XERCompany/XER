#include "ui/widgets/audio_player_widget.h"
#include "ui/core/window.h"
#include "ui/core/application.h"
#include "app/app_audio_controller.h"

#include <memory>
#include <thread>
#include <chrono>

int main() {
    // Initialize UI application
    auto& ui_app = XER::UI::Core::Application::Instance();
    if (!ui_app.Initialize()) {
        return 1;
    }

    // Create audio controller
    auto audio_controller = std::make_unique<App::AudioController>();
    if (!audio_controller->Initialize()) {
        return 1;
    }

    // Load a test audio file (placeholder - user should provide actual path)
    // std::string error;
    // if (!audio_controller->LoadFromFile("path/to/audio.mp3", &error)) {
    //     return 1;
    // }

    // Create window
    auto window = std::make_shared<XER::UI::Core::Window>();
    if (!window->Create("Audio Player Demo", 800, 200)) {
        return 1;
    }

    // Create audio player widget
    auto audio_widget = std::make_shared<XER::UI::Widgets::AudioPlayerWidget>(audio_controller.get());
    window->SetContent(audio_widget);

    // Set up close callback
    window->SetOnClose([&]() {
        audio_controller->Stop();
    });

    // Show window
    window->Show();

    // Run UI event loop
    return ui_app.Run();
}
