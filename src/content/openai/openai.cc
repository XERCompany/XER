#include "openai.h"

namespace Content::OpenAI {

OpenAISubsystem::OpenAISubsystem(const Config& config)
    : config_(config) {
}

OpenAISubsystem::~OpenAISubsystem() {
    Shutdown();
}

bool OpenAISubsystem::Initialize() {
    if (initialized_) {
        return false;
    }

    // Create client
    Client::OpenAIClient::Config client_config;
    client_config.api_key = config_.api_key;
    client_config.base_url = config_.base_url;
    client_ = std::make_shared<Client::OpenAIClient>(client_config);

    // Create processors
    image_processor_ = Image::ImageProcessorFactory::CreateOpenAIProcessor(client_);
    text_processor_ = Text::TextProcessorFactory::CreateOpenAIProcessor(client_);
    sandbox_ = Sandbox::SandboxFactory::CreatePythonSandbox();
    audio_transcriber_ = Audio::AudioTranscriberFactory::CreateWhisperTranscriber(client_);

    // Register default skills if enabled
    if (config_.register_default_skills) {
        Skills::SkillRegistry::GetInstance().RegisterDefaultSkills();
    }

    // Create server if enabled
    if (config_.enable_server) {
        server_ = Server::ServerFactory::CreateServer(config_.server_config);
        server_->SetClient(client_);
        server_->SetImageProcessor(image_processor_);
        server_->SetTextProcessor(text_processor_);
        server_->SetSandbox(sandbox_);

        if (!server_->Start()) {
            server_.reset();
        }
    }

    initialized_ = true;
    return true;
}

void OpenAISubsystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    // Stop server
    if (server_) {
        server_->Stop();
        server_.reset();
    }

    // Clear agents
    {
        std::lock_guard<std::mutex> lock(agents_mutex_);
        agents_.clear();
    }

    // Reset components
    image_processor_.reset();
    text_processor_.reset();
    sandbox_.reset();
    client_.reset();

    initialized_ = false;
}

std::shared_ptr<Client::OpenAIClient> OpenAISubsystem::GetClient() const {
    return client_;
}

std::shared_ptr<Server::IOpenAIServer> OpenAISubsystem::GetServer() const {
    return server_;
}

std::shared_ptr<Image::IImageProcessor> OpenAISubsystem::GetImageProcessor() const {
    return image_processor_;
}

std::shared_ptr<Text::ITextProcessor> OpenAISubsystem::GetTextProcessor() const {
    return text_processor_;
}

std::shared_ptr<Sandbox::ISandbox> OpenAISubsystem::GetSandbox() const {
    return sandbox_;
}

std::shared_ptr<Audio::IAudioTranscriber> OpenAISubsystem::GetAudioTranscriber() const {
    return audio_transcriber_;
}

void OpenAISubsystem::RegisterAgent(const std::string& name, std::shared_ptr<Agent::IAgent> agent) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    agents_[name] = agent;

    if (server_) {
        server_->RegisterAgent(name, agent);
    }
}

void OpenAISubsystem::UnregisterAgent(const std::string& name) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    agents_.erase(name);

    if (server_) {
        server_->UnregisterAgent(name);
    }
}

std::shared_ptr<Agent::IAgent> OpenAISubsystem::GetAgent(const std::string& name) const {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    auto it = agents_.find(name);
    if (it != agents_.end()) {
        return it->second;
    }
    return nullptr;
}

std::unique_ptr<Agent::CodingAgent> OpenAISubsystem::CreateCodingAgent() {
    auto agent = std::make_unique<Agent::CodingAgent>(client_);
    RegisterAgent("coding", std::shared_ptr<Agent::IAgent>(agent.get()));
    return agent;
}

std::unique_ptr<Agent::ImageAgent> OpenAISubsystem::CreateImageAgent() {
    auto agent = std::make_unique<Agent::ImageAgent>(client_);
    RegisterAgent("image", std::shared_ptr<Agent::IAgent>(agent.get()));
    return agent;
}

std::unique_ptr<Agent::ResearchAgent> OpenAISubsystem::CreateResearchAgent() {
    auto agent = std::make_unique<Agent::ResearchAgent>(client_);
    RegisterAgent("research", std::shared_ptr<Agent::IAgent>(agent.get()));
    return agent;
}

// OpenAIFactory implementation
std::unique_ptr<OpenAISubsystem> OpenAIFactory::CreateDefault() {
    OpenAISubsystem::Config config;
    return std::make_unique<OpenAISubsystem>(config);
}

std::unique_ptr<OpenAISubsystem> OpenAIFactory::CreateFromEnv() {
    OpenAISubsystem::Config config;

    const char* api_key = std::getenv("OPENAI_API_KEY");
    if (api_key) {
        config.api_key = api_key;
    }

    const char* base_url = std::getenv("OPENAI_BASE_URL");
    if (base_url) {
        config.base_url = base_url;
    }

    return std::make_unique<OpenAISubsystem>(config);
}

std::unique_ptr<OpenAISubsystem> OpenAIFactory::CreateWithServer(
    const std::string& api_key,
    int port) {

    OpenAISubsystem::Config config;
    config.api_key = api_key;
    config.enable_server = true;
    config.server_config.port = port;

    return std::make_unique<OpenAISubsystem>(config);
}

}  // namespace Content::OpenAI
