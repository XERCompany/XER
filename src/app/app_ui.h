#pragma once

#include <memory>
#include <string>

#include "app_audio_controller.h"

namespace App {

class AppUI {
public:
    AppUI();
    ~AppUI();

    // Initialization
    bool Initialize();
    void Shutdown();

    // Window management
    bool CreateMainWindow(const std::string& title, int width, int height);
    void ShowWindow();
    void HideWindow();
    void CloseWindow();

    // Audio widget integration
    void SetupAudioPlayerWidget(AudioController* controller);

    // Run UI event loop
    int Run();

    // State
    bool IsInitialized() const { return initialized_; }
    bool IsWindowVisible() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    bool initialized_;
};

}  // namespace App
