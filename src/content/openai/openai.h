#pragma once

// OpenAI Content Subsystem
// Provides comprehensive integration with OpenAI API including:
// - Client library for API interactions (using content/network HTTP)
// - Agent system with memory and tools
// - Skills system for extensible capabilities
// - Sandbox for secure code execution
// - Image processing and generation
// - Text processing and analysis
// - Audio transcription (using audio/ and Whisper)
// - Server for HTTP API endpoint
// - Integration with XER infrastructure (async_io, range-v3, absl, re2, libuv)
// - Hardware acceleration (GPU via hardware_abstraction)
// - DSP algorithms (audio/dsp_algorithms)
// - Utilities (string, JSON, validation)

#include "openai_types.h"

// Client
#include "client/openai_client.h"

// Agent
#include "agent/agent.h"

// Skills
#include "skills/skill.h"

// Sandbox
#include "sandbox/sandbox.h"

// Image
#include "image/image_processor.h"

// Text
#include "text/text_processor.h"

// Audio
#include "audio/audio_transcriber.h"
#include "audio/dsp_integration.h"

// Hardware
#include "hardware_integration.h"

// Utilities
#include "openai_utils.h"

// Server
#include "server/openai_server.h"

namespace Content::OpenAI {

// Main subsystem initializer
class OpenAISubsystem {
public:
    struct Config {
        std::string api_key;
        std::string base_url = "https://api.openai.com/v1";
        Server::ServerConfig server_config;
        bool enable_server = false;
        bool register_default_skills = true;
    };

    explicit OpenAISubsystem(const Config& config);
    ~OpenAISubsystem();

    // Initialize all components
    bool Initialize();
    void Shutdown();

    // Component access
    std::shared_ptr<Client::OpenAIClient> GetClient() const;
    std::shared_ptr<Server::IOpenAIServer> GetServer() const;
    std::shared_ptr<Image::IImageProcessor> GetImageProcessor() const;
    std::shared_ptr<Text::ITextProcessor> GetTextProcessor() const;
    std::shared_ptr<Sandbox::ISandbox> GetSandbox() const;
    std::shared_ptr<Audio::IAudioTranscriber> GetAudioTranscriber() const;
    std::shared_ptr<Hardware::HardwareManager> GetHardwareManager() const;
    std::shared_ptr<Hardware::GPUImageProcessor> GetGPUImageProcessor() const;

    // Agent management
    void RegisterAgent(const std::string& name, std::shared_ptr<Agent::IAgent> agent);
    void UnregisterAgent(const std::string& name);
    std::shared_ptr<Agent::IAgent> GetAgent(const std::string& name) const;

    // Convenience methods
    std::unique_ptr<Agent::CodingAgent> CreateCodingAgent();
    std::unique_ptr<Agent::ImageAgent> CreateImageAgent();
    std::unique_ptr<Agent::ResearchAgent> CreateResearchAgent();

private:
    Config config_;
    std::shared_ptr<Client::OpenAIClient> client_;
    std::shared_ptr<Server::IOpenAIServer> server_;
    std::shared_ptr<Image::IImageProcessor> image_processor_;
    std::shared_ptr<Text::ITextProcessor> text_processor_;
    std::shared_ptr<Sandbox::ISandbox> sandbox_;
    std::shared_ptr<Audio::IAudioTranscriber> audio_transcriber_;
    std::shared_ptr<Hardware::HardwareManager> hardware_manager_;
    std::shared_ptr<Hardware::GPUImageProcessor> gpu_image_processor_;
    std::map<std::string, std::shared_ptr<Agent::IAgent>> agents_;
    mutable std::mutex agents_mutex_;
    bool initialized_ = false;
};

// Factory for quick setup
class OpenAIFactory {
public:
    static std::unique_ptr<OpenAISubsystem> CreateDefault();
    static std::unique_ptr<OpenAISubsystem> CreateFromEnv();
    static std::unique_ptr<OpenAISubsystem> CreateWithServer(
        const std::string& api_key,
        int port = 8080
    );
};

}  // namespace Content::OpenAI
