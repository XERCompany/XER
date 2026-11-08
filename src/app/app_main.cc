#include "app/app_main.h"

#include <thread>
#include <chrono>
#include <iostream>

#include "app_command.h"
#include "audio_config_helper.h"
#include "content/network/socket/platform/socket_platform.h"
#include "cache/cache_manager.h"

using XER::AppCommand;

namespace XER::App {

Application::Application()
    : logger_(nullptr) {
}

Application::~Application() {
}

bool Application::Initialize(int argc, char** argv) {
    XER::Flux::Core::FluxConfigFromEnvironment();

    flux_core_ = XER::Flux::Core::FluxCore();
    XER::Flux::Core::FluxConfig config = flux_core_.context().config();

    logger_ = &flux_core_.context().logger();

    config.application_name = "Editor";
    logger_->ShouldLog(XER::Flux::Core::LogLevel::kInfo);
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
    auto& socket_platform = XER::Network::Socket::Platform::SocketPlatform::GetInstance();
    if (!socket_platform.Initialize(&error)) {
        logger_->Error("Network", error);
        return false;
    }
    
    // Cache manager
    auto& cache_manager = XER::Cache::CacheManager::Instance();
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
    
    // Audio input is optional — the node editor runs without it.
    audio_input_path_ = parsed.audio_input_path;
    audio_config_file_ = parsed.audio_config_file;
    
    return true;
}

bool Application::SetupAudioGraph() {
    if (audio_input_path_.empty()) {
        logger_->Info("NodeGraph", "No audio input — skipping audio graph setup");
        return true;
    }
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
    if (!app_ui_.CreateMainWindow("XER Node Editor", 1280, 800)) {
        logger_->Error("UI", "Failed to create main window");
        return false;
    }

    // Node graph editor is the only content.
    app_ui_.SetupNodeGraphEditor();

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
    const bool has_audio = !audio_input_path_.empty();

    // Run UI event loop if UI is initialized; audio playback (when an input
    // file was given) is best-effort and must not block the editor.
    if (app_ui_.IsInitialized()) {
        if (has_audio && !StartPlayback()) {
            logger_->Warning("AudioController", "Playback failed — continuing in editor");
        }
        logger_->Info("Main", "Starting UI event loop");
        return app_ui_.Run();
    }

    if (!has_audio) {
        logger_->Error("Main", "No audio input and no UI — nothing to do");
        return 1;
    }
    if (!StartPlayback()) {
        return 1;
    }

    // Fallback: playback without UI (already started by StartPlayback)
    logger_->Info("AudioController", "Waiting for playback to complete");

    // Wait for playback to complete
    while (audio_controller_.GetState() == XER::Content::Audio::PlaybackState::kPlaying) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    logger_->Info("AudioController", "Playback finished");
    return 0;
}

}  // namespace XER::App