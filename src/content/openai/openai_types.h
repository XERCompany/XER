#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <map>

namespace Content::OpenAI {

// Message role types
enum class MessageRole {
    kSystem,
    kUser,
    kAssistant,
    kTool
};

// Content types
enum class ContentType {
    kText,
    kImage,
    kAudio,
    kVideo,
    kCode
};

// Model types
enum class ModelType {
    kGPT4,
    kGPT4Turbo,
    kGPT35Turbo,
    kDALL3,
    kWhisper,
    kCustom
};

// Response format
enum class ResponseFormat {
    kText,
    kJSON,
    kJSONSchema
};

// Tool/function call structure
struct ToolCall {
    std::string id;
    std::string type;
    std::string function_name;
    std::string function_arguments;
};

// Message content structure
struct MessageContent {
    ContentType type;
    std::string text;
    std::string mime_type;
    std::vector<uint8_t> binary_data;
    std::map<std::string, std::string> metadata;
};

// Chat message
struct ChatMessage {
    MessageRole role;
    std::vector<MessageContent> content;
    std::string name;
    std::vector<ToolCall> tool_calls;
    std::string tool_call_id;
};

// Tool definition
struct Tool {
    std::string type;
    struct Function {
        std::string name;
        std::string description;
        std::string parameters_json;  // JSON schema
    };
    Function function;
};

// Image generation parameters
struct ImageGenerationParams {
    std::string prompt;
    std::string model;
    int size_width = 1024;
    int size_height = 1024;
    int n = 1;
    std::string response_format = "url";
    std::string style = "vivid";
    std::string quality = "standard";
};

// Audio transcription parameters
struct AudioTranscriptionParams {
    std::string file_path;
    std::string model = "whisper-1";
    std::string language;
    std::string prompt;
    float temperature = 0.0f;
    std::string response_format = "json";
    int timestamp_granularities = 0;
};

// Completion options
struct CompletionOptions {
    std::string model;
    float temperature = 1.0f;
    float top_p = 1.0f;
    int n = 1;
    bool stream = false;
    std::vector<std::string> stop;
    int max_tokens = 0;
    float presence_penalty = 0.0f;
    float frequency_penalty = 0.0f;
    std::map<std::string, int> logit_bias;
    std::string user;
    ResponseFormat response_format = ResponseFormat::kText;
    std::vector<Tool> tools;
    std::string tool_choice = "auto";
    int seed = -1;
};

// Response structure
struct CompletionResponse {
    std::string id;
    std::string object;
    int created;
    std::string model;
    std::vector<ChatMessage> choices;
    struct Usage {
        int prompt_tokens;
        int completion_tokens;
        int total_tokens;
    };
    Usage usage;
    std::string finish_reason;
};

// Image generation response
struct ImageResponse {
    std::string revised_prompt;
    std::string url;
    std::vector<uint8_t> b64_json;
};

// Audio transcription response
struct AudioTranscriptionResponse {
    std::string text;
    std::string language;
    float duration;
    std::vector<std::pair<float, float>> segments;
};

// Error structure
struct APIError {
    std::string message;
    std::string type;
    std::string param;
    std::string code;
};

// Callback types
using StreamCallback = std::function<void(const std::string& chunk)>;
using CompletionCallback = std::function<void(const CompletionResponse&)>;
using ErrorCallback = std::function<void(const APIError&)>;

}  // namespace Content::OpenAI
