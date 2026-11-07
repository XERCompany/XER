#pragma once

#include <string>
#include "app/app_audio_controller.h"
#include "app/app_audio_processor.h"
#include "app/app_node_graph_manager.h"
#include "app/app_ui.h"
#include "flux/core/flux_core.h"
#include "flux/core/logger.h"

namespace App {

class Application {
public:
    Application();
    ~Application();

    // Initialize application
    bool Initialize(int argc, char** argv);
    int Run();

private:
    bool InitializeSystems();
    bool ParseCommandLine(int argc, char** argv);
    bool SetupAudioGraph();
    bool StartPlayback();
    bool SetupUI();

    // Systems
    flux::core::FluxCore flux_core_;
    flux::core::Logger* logger_;

    // App components
    AudioController audio_controller_;
    AudioProcessor audio_processor_;
    NodeGraphManager node_graph_manager_;
    AppUI app_ui_;

    // Configuration
    std::string audio_input_path_;
    std::string audio_config_file_;
};

}  // namespace App
