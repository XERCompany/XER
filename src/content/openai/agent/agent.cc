#include "agent.h"
#include "../../../async_io/io_thread_pool.h"
#include <algorithm>
#include <chrono>
#include <ctime>

namespace Content::OpenAI::Agent {

BaseAgent::BaseAgent(
    const AgentConfig& config,
    std::shared_ptr<Client::OpenAIClient> client)
    : config_(config),
      client_(client) {
}

BaseAgent::~BaseAgent() = default;

std::future<CompletionResponse> BaseAgent::Process(
    const std::string& input,
    const CompletionOptions& options) {

    std::promise<CompletionResponse> promise;
    auto future = promise.get_future();

    ProcessAsync(
        input,
        [&promise](const CompletionResponse& response) {
            promise.set_value(response);
        },
        [&promise](const APIError& error) {
            CompletionResponse response;
            response.id = "error";
            response.choices.push_back({MessageRole::kAssistant,
                {{ContentType::kText, error.message}}, "", {}, ""});
            promise.set_value(response);
        }
    );

    return future;
}

void BaseAgent::ProcessAsync(
    const std::string& input,
    CompletionCallback on_complete,
    ErrorCallback on_error) {

    SetState(AgentState::kThinking);

    CompletionOptions options;
    options.model = config_.model;
    options.temperature = config_.temperature;
    options.max_tokens = config_.max_tokens;
    options.tools = config_.enable_tools ? config_.tools : std::vector<Tool>{};
    options.tool_choice = config_.tool_choice;

    auto messages = BuildMessages(input);

    client_->CreateChatCompletionAsync(
        messages,
        options,
        [this, on_complete](const CompletionResponse& response) {
            // Process tool calls if present
            for (const auto& choice : response.choices) {
                for (const auto& tool_call : choice.tool_calls) {
                    ProcessToolCall(tool_call);
                }
            }

            // Add to memory if enabled
            if (config_.enable_memory && !response.choices.empty()) {
                MemoryEntry memory;
                memory.content = response.choices[0].content[0].text;
                memory.timestamp = std::to_string(std::time(nullptr));
                memory.relevance_score = 1.0f;
                AddMemory(memory);
            }

            SetState(AgentState::kIdle);
            if (on_complete) {
                on_complete(response);
            }
        },
        [this, on_error](const APIError& error) {
            SetState(AgentState::kError);
            if (on_error) {
                on_error(error);
            }
        }
    );
}

void BaseAgent::AddMemory(const MemoryEntry& memory) {
    std::lock_guard<std::mutex> lock(memory_mutex_);
    memory_.push_back(memory);

    // Enforce memory limit
    if (config_.memory_limit > 0 && memory_.size() > static_cast<size_t>(config_.memory_limit)) {
        memory_.erase(memory_.begin(), memory_.begin() + (memory_.size() - config_.memory_limit));
    }
}

std::vector<MemoryEntry> BaseAgent::RetrieveMemory(
    const std::string& query,
    int limit) {

    std::lock_guard<std::mutex> lock(memory_mutex_);

    // Simple relevance scoring based on keyword matching
    std::vector<std::pair<float, MemoryEntry>> scored;

    for (const auto& memory : memory_) {
        float score = 0.0f;
        // Count matching words
        std::string lower_content = memory.content;
        std::string lower_query = query;
        std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

        size_t pos = 0;
        while ((pos = lower_content.find(lower_query, pos)) != std::string::npos) {
            score += 1.0f;
            pos += lower_query.length();
        }

        scored.push_back({score, memory});
    }

    // Sort by relevance
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) {
            return a.first > b.first;
        }
    );

    // Return top results
    std::vector<MemoryEntry> results;
    for (int i = 0; i < limit && i < static_cast<int>(scored.size()); ++i) {
        scored[i].second.relevance_score = scored[i].first;
        results.push_back(scored[i].second);
    }

    return results;
}

void BaseAgent::ClearMemory() {
    std::lock_guard<std::mutex> lock(memory_mutex_);
    memory_.clear();
}

AgentState BaseAgent::GetState() const {
    return state_.load();
}

void BaseAgent::SetState(AgentState state) {
    state_.store(state);
}

const AgentConfig& BaseAgent::GetConfig() const {
    return config_;
}

void BaseAgent::UpdateConfig(const AgentConfig& config) {
    config_ = config;
}

void BaseAgent::RegisterTool(const Tool& tool) {
    std::lock_guard<std::mutex> lock(tools_mutex_);
    tools_.push_back(tool);
}

void BaseAgent::UnregisterTool(const std::string& tool_name) {
    std::lock_guard<std::mutex> lock(tools_mutex_);
    tools_.erase(
        std::remove_if(tools_.begin(), tools_.end(),
            [&tool_name](const Tool& t) {
                return t.function.name == tool_name;
            }
        ),
        tools_.end()
    );
}

std::vector<Tool> BaseAgent::GetTools() const {
    std::lock_guard<std::mutex> lock(tools_mutex_);
    return tools_;
}

std::vector<ChatMessage> BaseAgent::BuildMessages(const std::string& input) {
    std::vector<ChatMessage> messages;

    // Add system prompt
    if (!config_.system_prompt.empty()) {
        ChatMessage system_msg;
        system_msg.role = MessageRole::kSystem;
        system_msg.content.push_back({ContentType::kText, config_.system_prompt});
        messages.push_back(system_msg);
    }

    // Add relevant memory context
    if (config_.enable_memory) {
        auto relevant_memory = RetrieveMemory(input, 3);
        for (const auto& mem : relevant_memory) {
            if (mem.relevance_score > 0.5f) {
                ChatMessage memory_msg;
                memory_msg.role = MessageRole::kSystem;
                memory_msg.content.push_back({ContentType::kText,
                    "Context: " + mem.content});
                messages.push_back(memory_msg);
            }
        }
    }

    // Add user input
    ChatMessage user_msg;
    user_msg.role = MessageRole::kUser;
    user_msg.content.push_back({ContentType::kText, input});
    messages.push_back(user_msg);

    return messages;
}

void BaseAgent::ProcessToolCall(const ToolCall& tool_call) {
    SetState(AgentState::kExecuting);

    // TODO: Implement actual tool execution
    // This would involve dispatching to registered tool handlers

    SetState(AgentState::kIdle);
}

// CodingAgent implementation
CodingAgent::CodingAgent(std::shared_ptr<Client::OpenAIClient> client)
    : BaseAgent(AgentConfig{
        .name = "CodingAgent",
        .description = "Specialized agent for code generation and analysis",
        .system_prompt = "You are an expert programmer. Provide clear, efficient, and well-documented code.",
        .model = "gpt-4",
        .temperature = 0.2f,
        .max_tokens = 4096,
        .capabilities = {AgentCapability::kCodeGeneration, AgentCapability::kToolUse},
        .enable_memory = true,
        .enable_tools = true
    }, client) {
}

// ImageAgent implementation
ImageAgent::ImageAgent(std::shared_ptr<Client::OpenAIClient> client)
    : BaseAgent(AgentConfig{
        .name = "ImageAgent",
        .description = "Specialized agent for image generation and analysis",
        .system_prompt = "You are an expert in visual arts and image generation.",
        .model = "gpt-4-vision-preview",
        .temperature = 0.8f,
        .max_tokens = 1024,
        .capabilities = {AgentCapability::kImageGeneration, AgentCapability::kTextGeneration},
        .enable_memory = true,
        .enable_tools = false
    }, client) {
}

std::future<ImageResponse> ImageAgent::GenerateImage(
    const ImageGenerationParams& params) {

    return client_->GenerateImage(params);
}

// ResearchAgent implementation
ResearchAgent::ResearchAgent(std::shared_ptr<Client::OpenAIClient> client)
    : BaseAgent(AgentConfig{
        .name = "ResearchAgent",
        .description = "Specialized agent for research and analysis",
        .system_prompt = "You are a research assistant. Provide thorough, well-sourced analysis.",
        .model = "gpt-4",
        .temperature = 0.5f,
        .max_tokens = 4096,
        .capabilities = {AgentCapability::kReasoning, AgentCapability::kMemory},
        .enable_memory = true,
        .enable_tools = true
    }, client) {
}

void ResearchAgent::EnableWebSearch(bool enable) {
    // TODO: Implement web search tool registration
}

void ResearchAgent::EnableCodeExecution(bool enable) {
    // TODO: Implement code execution sandbox integration
}

// AgentFactory implementation
std::unique_ptr<IAgent> AgentFactory::CreateAgent(
    const AgentConfig& config,
    std::shared_ptr<Client::OpenAIClient> client) {

    return std::make_unique<BaseAgent>(config, client);
}

std::unique_ptr<CodingAgent> AgentFactory::CreateCodingAgent(
    std::shared_ptr<Client::OpenAIClient> client) {

    return std::make_unique<CodingAgent>(client);
}

std::unique_ptr<ImageAgent> AgentFactory::CreateImageAgent(
    std::shared_ptr<Client::OpenAIClient> client) {

    return std::make_unique<ImageAgent>(client);
}

std::unique_ptr<ResearchAgent> AgentFactory::CreateResearchAgent(
    std::shared_ptr<Client::OpenAIClient> client) {

    return std::make_unique<ResearchAgent>(client);
}

}  // namespace Content::OpenAI::Agent
