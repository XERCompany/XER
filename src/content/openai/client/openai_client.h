#pragma once

#include "../openai_types.h"
#include "../../network/http/http_transaction.h"
#include "../../network/url/url_request.h"
#include <memory>
#include <future>
#include <optional>

namespace Content::OpenAI::Client {

class OpenAIClient {
public:
    struct Config {
        std::string api_key;
        std::string organization_id;
        std::string base_url = "https://api.openai.com/v1";
        int timeout_seconds = 60;
        int max_retries = 3;
        std::string user_agent = "XER-OpenAI-Client/1.0";
    };

    explicit OpenAIClient(const Config& config);
    ~OpenAIClient();

    // Chat completions
    std::future<CompletionResponse> CreateChatCompletion(
        const std::vector<ChatMessage>& messages,
        const CompletionOptions& options = CompletionOptions()
    );

    std::future<CompletionResponse> CreateChatCompletionAsync(
        const std::vector<ChatMessage>& messages,
        const CompletionOptions& options = CompletionOptions(),
        CompletionCallback on_complete = nullptr,
        ErrorCallback on_error = nullptr
    );

    void CreateChatCompletionStream(
        const std::vector<ChatMessage>& messages,
        const CompletionOptions& options,
        StreamCallback on_chunk,
        CompletionCallback on_complete = nullptr,
        ErrorCallback on_error = nullptr
    );

    // Image generation
    std::future<ImageResponse> GenerateImage(
        const ImageGenerationParams& params
    );

    std::future<ImageResponse> GenerateImageAsync(
        const ImageGenerationParams& params,
        CompletionCallback on_complete = nullptr,
        ErrorCallback on_error = nullptr
    );

    // Audio transcription
    std::future<AudioTranscriptionResponse> TranscribeAudio(
        const AudioTranscriptionParams& params
    );

    std::future<AudioTranscriptionResponse> TranscribeAudioAsync(
        const AudioTranscriptionParams& params,
        CompletionCallback on_complete = nullptr,
        ErrorCallback on_error = nullptr
    );

    // Configuration
    void SetAPIKey(const std::string& api_key);
    void SetBaseURL(const std::string& base_url);
    const Config& GetConfig() const { return config_; }

    // Rate limiting
    void SetRateLimit(int requests_per_minute);
    int GetRateLimit() const;

    // Usage tracking
    struct UsageStats {
        int total_requests;
        int total_tokens;
        int total_prompt_tokens;
        int total_completion_tokens;
    };
    UsageStats GetUsageStats() const;
    void ResetUsageStats();

private:
    Config config_;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Factory for creating clients
class ClientFactory {
public:
    static std::unique_ptr<OpenAIClient> CreateFromEnv();
    static std::unique_ptr<OpenAIClient> CreateFromConfigFile(
        const std::string& config_path
    );
    static std::unique_ptr<OpenAIClient> CreateDefault();
};

}  // namespace Content::OpenAI::Client
