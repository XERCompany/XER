#include "app/app_main.h"

#include <thread>
#include <chrono>
#include <iostream>

#include "app_command.h"
#include "audio_config_helper.h"
#include "content/network/socket/platform/socket_platform.h"
#include "cache/cache_manager.h"

namespace App {

Application::Application()
    : logger_(nullptr) {
}

Application::~Application() {
}

bool Application::Initialize(int argc, char** argv) {
    flux::core::FluxConfigFromEnvironment();

    flux_core_ = flux::core::FluxCore();
    flux::core::FluxConfig config = flux_core_.context().config();

    logger_ = &flux_core_.context().logger();

    config.application_name = "Editor";
    logger_->ShouldLog(flux::core::LogLevel::kInfo);
    logger_->UpdateConfig(config);

    flux_core_.context().RefreshTerminalWindow();

    if (!ParseCommandLine(argc, argv)) {
        return false;
    }

    if (!InitializeSystems()) {
        return false;
    }

    if (!SetupAudioGraph()) {
        return false;
    }

    if (!SetupUI()) {
        logger_->Warning("UI", "Failed to setup UI, running in headless mode");
    }

    return true;
}

bool Application::InitializeSystems() {
    std::string error;
    
    // Socket platform
    auto& socket_platform = network::socket::platform::SocketPlatform::GetInstance();
    if (!socket_platform.Initialize(&error)) {
        logger_->Error("Network", error);
        return false;
    }
    
    // Cache manager
    auto& cache_manager = Engine::Cache::CacheManager::Instance();
    logger_->Info("main", "FluxCore initialized successfully.");
    logger_->Info("Cache", "Cache root directory: " + cache_manager.RootDirectory().string());
    
    // Audio controller
    if (!audio_controller_.Initialize()) {
        logger_->Error("AudioController", "Failed to initialize audio controller");
        return false;
    }
    
    // Audio processor
    if (!audio_processor_.Initialize()) {
        logger_->Error("AudioProcessor", "Failed to initialize audio processor");
        return false;
    }
    
    // Node graph manager
    if (!node_graph_manager_.Initialize()) {
        logger_->Error("NodeGraphManager", "Failed to initialize node graph manager");
        return false;
    }
    
    return true;
}

bool Application::ParseCommandLine(int argc, char** argv) {
    auto parsed = AppCommand::Parse(argc, argv);
    
    if (parsed.type == AppCommand::Type::kHelp) {
        std::cout << AppCommand::BuildHelpText(argv[0]) << std::endl;
        return false;
    }
    
    std::string config_error;
    if (!XER::AudioConfigHelper::ApplyAudioConfigFile(&parsed, &config_error)) {
        // Config file is optional
    }
    
    if (!parsed.audio_config_file.empty()) {
        logger_->Info("Audio", "Audio config file: " + parsed.audio_config_file);
    }
    
    if (parsed.audio_input_path.empty()) {
        logger_->Error("Audio", "Audio input path is empty");
        return false;
    }
    
    audio_input_path_ = parsed.audio_input_path;
    audio_config_file_ = parsed.audio_config_file;
    
    return true;
}

bool Application::SetupAudioGraph() {
    // Register audio input node
    if (!node_graph_manager_.RegisterAudioInputNode(audio_input_path_)) {
        logger_->Error("NodeGraph", "Failed to register audio input node");
        return false;
    }
    
    // Create graph
    if (!node_graph_manager_.CreateGraph()) {
        logger_->Error("NodeGraph", "Failed to create graph");
        return false;
    }
    
    logger_->Info("NodeGraph", "Graph has " + std::to_string(node_graph_manager_.GetNodeCount()) + " nodes");
    
    // Execute graph
    if (!node_graph_manager_.ExecuteGraph()) {
        logger_->Error("NodeGraph", "Graph execution failed: " + node_graph_manager_.GetLastResult().error);
        return false;
    }
    
    logger_->Info("NodeGraph", "Graph executed successfully: " + 
                  std::to_string(node_graph_manager_.GetLastResult().evaluated) + " nodes evaluated");
    
    std::string graph_json = node_graph_manager_.SerializeGraph();
    logger_->Info("NodeGraph", "Graph JSON: " + graph_json);
    
    return true;
}

bool Application::SetupUI() {
    // Initialize UI system
    if (!app_ui_.Initialize()) {
        logger_->Error("UI", "Failed to initialize UI system");
        return false;
    }

    // Create main window
    if (!app_ui_.CreateMainWindow("XER Audio Editor", 800, 200)) {
        logger_->Error("UI", "Failed to create main window");
        return false;
    }

    // Setup audio player widget
    app_ui_.SetupAudioPlayerWidget(&audio_controller_);

    // Show window
    app_ui_.ShowWindow();

    logger_->Info("UI", "UI initialized successfully");
    return true;
}

bool Application::StartPlayback() {
    logger_->Info("AudioController", "Loading audio file: " + audio_input_path_);

    std::string load_error;
    if (!audio_controller_.LoadFromFile(audio_input_path_, &load_error)) {
        logger_->Error("AudioController", "Failed to load audio file: " + load_error);
        return false;
    }

    logger_->Info("AudioController", "Audio loaded successfully");
    logger_->Info("AudioController", "Duration: " + std::to_string(audio_controller_.GetDuration()) + " seconds");

    if (!audio_controller_.Play()) {
        logger_->Error("AudioController", "Failed to start playback");
        return false;
    }

    logger_->Info("AudioController", "Playback started");
    return true;
}

int Application::Run() {
    if (!StartPlayback()) {
        return 1;
    }

    // Run UI event loop if UI is initialized
    if (app_ui_.IsInitialized()) {
        logger_->Info("Main", "Starting UI event loop");
        return app_ui_.Run();
    }

    // Fallback: playback without UI (already started by StartPlayback)
    logger_->Info("AudioController", "Waiting for playback to complete");

    // Wait for playback to complete
    while (audio_controller_.GetState() == Content::Audio::PlaybackState::kPlaying) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    logger_->Info("AudioController", "Playback finished");
    return 0;
}

}  // namespace App
