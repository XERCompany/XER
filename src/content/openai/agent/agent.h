#pragma once

#include "../openai_types.h"
#include "../client/openai_client.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace Content::OpenAI::Agent {

// Agent capabilities
enum class AgentCapability {
    kTextGeneration,
    kImageGeneration,
    kAudioTranscription,
    kCodeGeneration,
    kToolUse,
    kReasoning,
    kMemory
};

// Agent state
enum class AgentState {
    kIdle,
    kThinking,
    kExecuting,
    kWaiting,
    kError
};

// Agent memory entry
struct MemoryEntry {
    std::string content;
    std::string timestamp;
    std::string embedding;
    float relevance_score;
    std::map<std::string, std::string> metadata;
};

// Agent configuration
struct AgentConfig {
    std::string name;
    std::string description;
    std::string system_prompt;
    std::string model;
    float temperature = 0.7f;
    int max_tokens = 2048;
    std::vector<AgentCapability> capabilities;
    bool enable_memory = true;
    int memory_limit = 1000;
    bool enable_tools = true;
    std::vector<Tool> tools;
    std::string tool_choice = "auto";
};

// Agent interface
class IAgent {
public:
    virtual ~IAgent() = default;

    // Core operations
    virtual std::future<CompletionResponse> Process(
        const std::string& input,
        const CompletionOptions& options = CompletionOptions()
    ) = 0;

    virtual void ProcessAsync(
        const std::string& input,
        CompletionCallback on_complete,
        ErrorCallback on_error = nullptr
    ) = 0;

    // Memory management
    virtual void AddMemory(const MemoryEntry& memory) = 0;
    virtual std::vector<MemoryEntry> RetrieveMemory(
        const std::string& query,
        int limit = 10
    ) = 0;
    virtual void ClearMemory() = 0;

    // State management
    virtual AgentState GetState() const = 0;
    virtual void SetState(AgentState state) = 0;

    // Configuration
    virtual const AgentConfig& GetConfig() const = 0;
    virtual void UpdateConfig(const AgentConfig& config) = 0;

    // Tools
    virtual void RegisterTool(const Tool& tool) = 0;
    virtual void UnregisterTool(const std::string& tool_name) = 0;
    virtual std::vector<Tool> GetTools() const = 0;
};

// Base agent implementation
class BaseAgent : public IAgent {
public:
    explicit BaseAgent(
        const AgentConfig& config,
        std::shared_ptr<Client::OpenAIClient> client
    );
    ~BaseAgent() override;

    // IAgent implementation
    std::future<CompletionResponse> Process(
        const std::string& input,
        const CompletionOptions& options = CompletionOptions()
    ) override;

    void ProcessAsync(
        const std::string& input,
        CompletionCallback on_complete,
        ErrorCallback on_error = nullptr
    ) override;

    void AddMemory(const MemoryEntry& memory) override;
    std::vector<MemoryEntry> RetrieveMemory(
        const std::string& query,
        int limit = 10
    ) override;
    void ClearMemory() override;

    AgentState GetState() const override;
    void SetState(AgentState state) override;

    const AgentConfig& GetConfig() const override;
    void UpdateConfig(const AgentConfig& config) override;

    void RegisterTool(const Tool& tool) override;
    void UnregisterTool(const std::string& tool_name) override;
    std::vector<Tool> GetTools() const override;

protected:
    virtual std::vector<ChatMessage> BuildMessages(
        const std::string& input
    );
    virtual void ProcessToolCall(const ToolCall& tool_call);

    AgentConfig config_;
    std::shared_ptr<Client::OpenAIClient> client_;
    std::vector<MemoryEntry> memory_;
    std::vector<Tool> tools_;
    std::atomic<AgentState> state_{AgentState::kIdle};
    mutable std::mutex memory_mutex_;
    mutable std::mutex tools_mutex_;
};

// Specialized agent types
class CodingAgent : public BaseAgent {
public:
    explicit CodingAgent(
        std::shared_ptr<Client::OpenAIClient> client
    );
};

class ImageAgent : public BaseAgent {
public:
    explicit ImageAgent(
        std::shared_ptr<Client::OpenAIClient> client
    );

    std::future<ImageResponse> GenerateImage(
        const ImageGenerationParams& params
    );
};

class ResearchAgent : public BaseAgent {
public:
    explicit ResearchAgent(
        std::shared_ptr<Client::OpenAIClient> client
    );

    void EnableWebSearch(bool enable);
    void EnableCodeExecution(bool enable);
};

// Agent factory
class AgentFactory {
public:
    static std::unique_ptr<IAgent> CreateAgent(
        const AgentConfig& config,
        std::shared_ptr<Client::OpenAIClient> client
    );

    static std::unique_ptr<CodingAgent> CreateCodingAgent(
        std::shared_ptr<Client::OpenAIClient> client
    );

    static std::unique_ptr<ImageAgent> CreateImageAgent(
        std::shared_ptr<Client::OpenAIClient> client
    );

    static std::unique_ptr<ResearchAgent> CreateResearchAgent(
        std::shared_ptr<Client::OpenAIClient> client
    );
};

}  // namespace Content::OpenAI::Agent
