#pragma once

#include "../openai_types.h"
#include "../client/openai_client.h"
#include "../agent/agent.h"
#include "../skills/skill.h"
#include "../sandbox/sandbox.h"
#include "../image/image_processor.h"
#include "../text/text_processor.h"
#include <memory>
#include <string>
#include <map>
#include <functional>
#include <atomic>

namespace Content::OpenAI::Server {

// Server configuration
struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int max_connections = 100;
    int timeout_seconds = 30;
    bool enable_cors = true;
    bool enable_auth = false;
    std::string auth_token;
    bool enable_rate_limiting = true;
    int rate_limit_per_minute = 60;
    bool enable_logging = true;
    std::string log_level = "info";
};

// Request types
enum class RequestType {
    kChatCompletion,
    kImageGeneration,
    kAudioTranscription,
    kTextProcessing,
    kAgentProcess,
    kSkillExecute,
    kSandboxExecute
};

// Server request
struct ServerRequest {
    RequestType type;
    std::string request_id;
    std::string client_id;
    std::map<std::string, std::string> headers;
    std::string body;
    std::map<std::string, std::string> parameters;
};

// Server response
struct ServerResponse {
    std::string request_id;
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
    std::map<std::string, std::string> headers;
    bool success = true;
    std::string error_message;
};

// Server statistics
struct ServerStats {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> successful_requests{0};
    std::atomic<uint64_t> failed_requests{0};
    std::atomic<uint64_t> active_connections{0};
    std::chrono::system_clock::time_point start_time;
};

// OpenAI server interface
class IOpenAIServer {
public:
    virtual ~IOpenAIServer() = default;

    // Server lifecycle
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;

    // Configuration
    virtual void SetConfig(const ServerConfig& config) = 0;
    virtual ServerConfig GetConfig() const = 0;

    // Statistics
    virtual ServerStats GetStats() const = 0;
    virtual void ResetStats() = 0;

    // Component management
    virtual void SetClient(std::shared_ptr<Client::OpenAIClient> client) = 0;
    virtual void RegisterAgent(const std::string& name, std::shared_ptr<Agent::IAgent> agent) = 0;
    virtual void UnregisterAgent(const std::string& name) = 0;
};

// Main OpenAI server implementation
class OpenAIServer : public IOpenAIServer {
public:
    explicit OpenAIServer(const ServerConfig& config = ServerConfig());
    ~OpenAIServer() override;

    // IOpenAIServer implementation
    bool Start() override;
    void Stop() override;
    bool IsRunning() const override;

    void SetConfig(const ServerConfig& config) override;
    ServerConfig GetConfig() const override;

    ServerStats GetStats() const override;
    void ResetStats() override;

    void SetClient(std::shared_ptr<Client::OpenAIClient> client) override;
    void RegisterAgent(const std::string& name, std::shared_ptr<Agent::IAgent> agent) override;
    void UnregisterAgent(const std::string& name) override;

    // Additional component management
    void SetImageProcessor(std::shared_ptr<Image::IImageProcessor> processor);
    void SetTextProcessor(std::shared_ptr<Text::ITextProcessor> processor);
    void SetSandbox(std::shared_ptr<Sandbox::ISandbox> sandbox);

    // Request handling
    std::future<ServerResponse> HandleRequest(const ServerRequest& request);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    ServerResponse HandleChatCompletion(const ServerRequest& request);
    ServerResponse HandleImageGeneration(const ServerRequest& request);
    ServerResponse HandleAudioTranscription(const ServerRequest& request);
    ServerResponse HandleTextProcessing(const ServerRequest& request);
    ServerResponse HandleAgentProcess(const ServerRequest& request);
    ServerResponse HandleSkillExecute(const ServerRequest& request);
    ServerResponse HandleSandboxExecute(const ServerRequest& request);
};

// Server factory
class ServerFactory {
public:
    static std::unique_ptr<IOpenAIServer> CreateServer(
        const ServerConfig& config = ServerConfig()
    );

    static std::unique_ptr<IOpenAIServer> CreateDefaultServer();
};

// Middleware system
class Middleware {
public:
    using Handler = std::function<bool(const ServerRequest&, ServerResponse&)>;

    virtual ~Middleware() = default;
    virtual bool Process(const ServerRequest& request, ServerResponse& response) = 0;
};

class AuthMiddleware : public Middleware {
public:
    explicit AuthMiddleware(const std::string& token);
    bool Process(const ServerRequest& request, ServerResponse& response) override;

private:
    std::string auth_token_;
};

class RateLimitMiddleware : public Middleware {
public:
    explicit RateLimitMiddleware(int requests_per_minute);
    bool Process(const ServerRequest& request, ServerResponse& response) override;

private:
    int rate_limit_;
    std::map<std::string, std::vector<std::chrono::system_clock::time_point>> request_history_;
    mutable std::mutex mutex_;
};

class LoggingMiddleware : public Middleware {
public:
    explicit LoggingMiddleware(const std::string& log_level = "info");
    bool Process(const ServerRequest& request, ServerResponse& response) override;

private:
    std::string log_level_;
};

}  // namespace Content::OpenAI::Server
