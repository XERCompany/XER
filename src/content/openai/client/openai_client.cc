#include "openai_client.h"
#include "../../../async_io/io_thread_pool.h"
#include "../../../async_io/async_task_queue.h"
#include "../../network/http/http_transaction.h"
#include "../../network/url/url_request.h"
#include "../../network/url/url_request_util.h"
#include <sstream>
#include <mutex>
#include <atomic>
#include <absl/strings/str_format.h>
#include <absl/strings/str_join.h>

namespace Content::OpenAI::Client {

class OpenAIClient::Impl {
public:
    Impl(const Config& config)
        : config_(config),
          thread_pool_(IO::AsyncIO::IOThreadPool::GetSharedInstance()),
          usage_stats_{0, 0, 0, 0} {
    }

    ~Impl() = default;

    Config config_;
    IO::AsyncIO::IOThreadPool& thread_pool_;
    std::mutex usage_mutex_;
    UsageStats usage_stats_;
    int rate_limit_ = 0;
    std::atomic<int> request_count_{0};
    
    // Helper methods
    static std::string BuildChatCompletionBody(
        const std::vector<ChatMessage>& messages,
        const CompletionOptions& options);
    
    static CompletionResponse ParseChatCompletionResponse(const std::string& body);
};

OpenAIClient::OpenAIClient(const Config& config)
    : config_(config),
      impl_(std::make_unique<Impl>(config)) {
}

OpenAIClient::~OpenAIClient() = default;

std::future<CompletionResponse> OpenAIClient::CreateChatCompletion(
    const std::vector<ChatMessage>& messages,
    const CompletionOptions& options) {

    std::promise<CompletionResponse> promise;
    auto future = promise.get_future();

    CreateChatCompletionAsync(
        messages,
        options,
        [&promise](const CompletionResponse& response) {
            promise.set_value(response);
        },
        [&promise](const APIError& error) {
            // Convert error to response with error info
            CompletionResponse response;
            response.id = "error";
            response.choices.push_back({MessageRole::kAssistant,
                {{ContentType::kText, error.message}}, "", {}, ""});
            promise.set_value(response);
        }
    );

    return future;
}

std::future<CompletionResponse> OpenAIClient::CreateChatCompletionAsync(
    const std::vector<ChatMessage>& messages,
    const CompletionOptions& options,
    CompletionCallback on_complete,
    ErrorCallback on_error) {

    std::promise<CompletionResponse> promise;
    auto future = promise.get_future();

    impl_->thread_pool_.Enqueue([this, messages, options, on_complete, on_error, promise = std::move(promise)]() mutable {
        // Rate limiting check
        if (impl_->rate_limit_ > 0) {
            int current = impl_->request_count_.fetch_add(1);
            if (current >= impl_->rate_limit_) {
                // Wait for rate limit reset (simplified)
                std::this_thread::sleep_for(std::chrono::seconds(60));
                impl_->request_count_.store(0);
            }
        }

        // Build HTTP request using XER network subsystem
        network::url::URLRequest request;
        request.url = impl_->config_.base_url + "/chat/completions";
        request.method = "POST";
        
        // Build JSON body from messages and options
        std::string body = impl_->BuildChatCompletionBody(messages, options);
        request.upload_data = body;
        
        // Set headers
        request.headers["Authorization"] = "Bearer " + impl_->config_.api_key;
        request.headers["Content-Type"] = "application/json";
        request.headers["User-Agent"] = impl_->config_.user_agent;

        // Execute HTTP transaction
        network::http::HttpTransaction transaction;
        auto result = transaction.Execute(request);

        CompletionResponse response;

        if (result.ok) {
            // Parse response
            response = impl_->ParseChatCompletionResponse(result.response.body);
        } else {
            // Handle error
            response.id = "error";
            response.object = "chat.completion";
            response.created = std::time(nullptr);
            response.model = options.model.empty() ? "gpt-4" : options.model;
            response.usage.prompt_tokens = 0;
            response.usage.completion_tokens = 0;
            response.usage.total_tokens = 0;
            response.finish_reason = "error";
            
            ChatMessage error_msg;
            error_msg.role = MessageRole::kAssistant;
            error_msg.content.push_back({ContentType::kText, result.error});
            response.choices.push_back(error_msg);
        }

        // Update usage stats
        {
            std::lock_guard<std::mutex> lock(impl_->usage_mutex_);
            impl_->usage_stats_.total_requests++;
            impl_->usage_stats_.total_tokens += response.usage.total_tokens;
            impl_->usage_stats_.total_prompt_tokens += response.usage.prompt_tokens;
            impl_->usage_stats_.total_completion_tokens += response.usage.completion_tokens;
        }

        if (on_complete) {
            on_complete(response);
        }
        promise.set_value(response);
    });

    return future;
}

void OpenAIClient::CreateChatCompletionStream(
    const std::vector<ChatMessage>& messages,
    const CompletionOptions& options,
    StreamCallback on_chunk,
    CompletionCallback on_complete,
    ErrorCallback on_error) {

    impl_->thread_pool_.Enqueue([this, messages, options, on_chunk, on_complete, on_error]() {
        // Simulated streaming
        std::string simulated_chunks[] = {"Hello", " from", " OpenAI", " API"};
        
        for (const auto& chunk : simulated_chunks) {
            if (on_chunk) {
                on_chunk(chunk);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (on_complete) {
            CompletionResponse response;
            response.id = "chatcmpl-stream";
            response.object = "chat.completion.chunk";
            response.created = std::time(nullptr);
            response.model = options.model;
            on_complete(response);
        }
    });
}

std::future<ImageResponse> OpenAIClient::GenerateImage(
    const ImageGenerationParams& params) {

    std::promise<ImageResponse> promise;
    auto future = promise.get_future();

    GenerateImageAsync(
        params,
        [&promise](const ImageResponse& response) {
            promise.set_value(response);
        },
        [&promise](const APIError& error) {
            ImageResponse response;
            response.revised_prompt = error.message;
            promise.set_value(response);
        }
    );

    return future;
}

std::future<ImageResponse> OpenAIClient::GenerateImageAsync(
    const ImageGenerationParams& params,
    CompletionCallback on_complete,
    ErrorCallback on_error) {

    std::promise<ImageResponse> promise;
    auto future = promise.get_future();

    impl_->thread_pool_.Enqueue([this, params, on_complete, on_error, promise = std::move(promise)]() mutable {
        ImageResponse response;
        response.revised_prompt = params.prompt;
        response.url = "https://example.com/generated-image.png";

        if (on_complete) {
            CompletionResponse comp_response;
            comp_response.id = "img-" + std::to_string(std::time(nullptr));
            on_complete(comp_response);
        }
        promise.set_value(response);
    });

    return future;
}

std::future<AudioTranscriptionResponse> OpenAIClient::TranscribeAudio(
    const AudioTranscriptionParams& params) {

    std::promise<AudioTranscriptionResponse> promise;
    auto future = promise.get_future();

    TranscribeAudioAsync(
        params,
        [&promise](const AudioTranscriptionResponse& response) {
            promise.set_value(response);
        },
        [&promise](const APIError& error) {
            AudioTranscriptionResponse response;
            response.text = error.message;
            promise.set_value(response);
        }
    );

    return future;
}

std::future<AudioTranscriptionResponse> OpenAIClient::TranscribeAudioAsync(
    const AudioTranscriptionParams& params,
    CompletionCallback on_complete,
    ErrorCallback on_error) {

    std::promise<AudioTranscriptionResponse> promise;
    auto future = promise.get_future();

    impl_->thread_pool_.Enqueue([this, params, on_complete, on_error, promise = std::move(promise)]() mutable {
        AudioTranscriptionResponse response;
        response.text = "Transcribed text from audio file";
        response.language = params.language.empty() ? "en" : params.language;
        response.duration = 10.5f;

        if (on_complete) {
            CompletionResponse comp_response;
            comp_response.id = "trans-" + std::to_string(std::time(nullptr));
            on_complete(comp_response);
        }
        promise.set_value(response);
    });

    return future;
}

void OpenAIClient::SetAPIKey(const std::string& api_key) {
    impl_->config_.api_key = api_key;
}

void OpenAIClient::SetBaseURL(const std::string& base_url) {
    impl_->config_.base_url = base_url;
}

void OpenAIClient::SetRateLimit(int requests_per_minute) {
    impl_->rate_limit_ = requests_per_minute;
}

int OpenAIClient::GetRateLimit() const {
    return impl_->rate_limit_;
}

OpenAIClient::UsageStats OpenAIClient::GetUsageStats() const {
    std::lock_guard<std::mutex> lock(impl_->usage_mutex_);
    return impl_->usage_stats_;
}

void OpenAIClient::ResetUsageStats() {
    std::lock_guard<std::mutex> lock(impl_->usage_mutex_);
    impl_->usage_stats_ = {0, 0, 0, 0};
}

std::unique_ptr<OpenAIClient> ClientFactory::CreateFromEnv() {
    Config config;
    const char* api_key = std::getenv("OPENAI_API_KEY");
    if (api_key) {
        config.api_key = api_key;
    }
    const char* org_id = std::getenv("OPENAI_ORGANIZATION");
    if (org_id) {
        config.organization_id = org_id;
    }
    return std::make_unique<OpenAIClient>(config);
}

std::unique_ptr<OpenAIClient> ClientFactory::CreateFromConfigFile(
    const std::string& config_path) {
    // TODO: Implement config file parsing
    Config config;
    return std::make_unique<OpenAIClient>(config);
}

std::unique_ptr<OpenAIClient> ClientFactory::CreateDefault() {
    Config config;
    return std::make_unique<OpenAIClient>(config);
}

// Helper methods
std::string OpenAIClient::Impl::BuildChatCompletionBody(
    const std::vector<ChatMessage>& messages,
    const CompletionOptions& options) {

    using namespace absl;
    
    std::string json = "{";
    
    // Add model
    json += str_format("\"model\":\"%s\"", options.model.empty() ? "gpt-4" : options.model.c_str());
    
    // Add messages
    json += ",\"messages\":[";
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i > 0) json += ",";
        json += "{";
        
        // Role
        std::string role_str;
        switch (messages[i].role) {
            case MessageRole::kSystem: role_str = "system"; break;
            case MessageRole::kUser: role_str = "user"; break;
            case MessageRole::kAssistant: role_str = "assistant"; break;
            case MessageRole::kTool: role_str = "tool"; break;
        }
        json += str_format("\"role\":\"%s\"", role_str.c_str());
        
        // Content
        json += ",\"content\":[";
        for (size_t j = 0; j < messages[i].content.size(); ++j) {
            if (j > 0) json += ",";
            json += "{";
            json += str_format("\"type\":\"%s\"",
                messages[i].content[j].type == ContentType::kText ? "text" : "image_url");
            json += str_format(",\"text\":\"%s\"", messages[i].content[j].text.c_str());
            json += "}";
        }
        json += "]";
        
        json += "}";
    }
    json += "]";
    
    // Add options
    if (options.temperature != 1.0f) {
        json += str_format(",\"temperature\":%.2f", options.temperature);
    }
    if (options.top_p != 1.0f) {
        json += str_format(",\"top_p\":%.2f", options.top_p);
    }
    if (options.n != 1) {
        json += str_format(",\"n\":%d", options.n);
    }
    if (options.stream) {
        json += ",\"stream\":true";
    }
    if (options.max_tokens > 0) {
        json += str_format(",\"max_tokens\":%d", options.max_tokens);
    }
    if (!options.stop.empty()) {
        json += ",\"stop\":[";
        for (size_t i = 0; i < options.stop.size(); ++i) {
            if (i > 0) json += ",";
            json += str_format("\"%s\"", options.stop[i].c_str());
        }
        json += "]";
    }
    if (options.presence_penalty != 0.0f) {
        json += str_format(",\"presence_penalty\":%.2f", options.presence_penalty);
    }
    if (options.frequency_penalty != 0.0f) {
        json += str_format(",\"frequency_penalty\":%.2f", options.frequency_penalty);
    }
    
    json += "}";
    
    return json;
}

CompletionResponse OpenAIClient::Impl::ParseChatCompletionResponse(
    const std::string& body) {

    // TODO: Implement proper JSON parsing using re2 or a JSON library
    // For now, return a mock response
    CompletionResponse response;
    response.id = "chatcmpl-" + std::to_string(std::time(nullptr));
    response.object = "chat.completion";
    response.created = std::time(nullptr);
    response.model = "gpt-4";
    response.usage.prompt_tokens = 100;
    response.usage.completion_tokens = 50;
    response.usage.total_tokens = 150;
    response.finish_reason = "stop";

    ChatMessage assistant_msg;
    assistant_msg.role = MessageRole::kAssistant;
    assistant_msg.content.push_back({ContentType::kText, "Response from OpenAI API"});
    response.choices.push_back(assistant_msg);

    return response;
}

}  // namespace Content::OpenAI::Client
