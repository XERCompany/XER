#include "openai_server.h"
#include "../../../async_io/io_thread_pool.h"
#include "content/network/http/http_message.h"
#include "content/network/http/http_server.h"
#include <json/json.h>
#include <chrono>
#include <sstream>
#include <random>
#include <thread>

namespace Content::OpenAI::Server {

namespace H = XER::Network::Http;

namespace {

// Flattened header map for ServerRequest (multimap -> joined values).
std::map<std::string, std::string> FlattenHeaders(const H::HttpHeaders& headers) {
    std::map<std::string, std::string> out;
    for (const auto& [name, value] : headers.MapView()) {
        auto it = out.find(name);
        if (it == out.end()) {
            out.emplace(name, value);
        } else {
            it->second += ", " + value;
        }
    }
    return out;
}

// JSON request bodies carry their fields in ServerRequest::parameters so the
// per-type handlers don't each re-parse the payload.
std::map<std::string, std::string> ParseParameters(const std::string& body) {
    std::map<std::string, std::string> out;
    if (body.empty()) return out;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    Json::Value root;
    if (!reader->parse(body.data(), body.data() + body.size(), &root, &errs) ||
        !root.isObject()) {
        return out;
    }
    for (const auto& member : root.getMemberNames()) {
        const Json::Value& v = root[member];
        if (v.isString()) {
            out[member] = v.asString();
        } else {
            out[member] = v.toStyledString();
        }
    }
    return out;
}

std::string NextRequestId() {
    static std::atomic<uint64_t> counter{0};
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "req_" + std::to_string(ms) + "_" +
           std::to_string(counter.fetch_add(1));
}

}  // namespace

class OpenAIServer::Impl {
public:
    Impl(const ServerConfig& config)
        : config_(config),
          thread_pool_(XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance()),
          is_running_(false) {
        stats_.start_time = std::chrono::system_clock::now();
    }

    ServerConfig config_;
    XER::AsyncIO::IO::Async::IOThreadPool& thread_pool_;
    std::atomic<bool> is_running_;
    ServerStats stats_;
    std::shared_ptr<Client::OpenAIClient> client_;
    std::map<std::string, std::shared_ptr<Agent::IAgent>> agents_;
    std::shared_ptr<Image::IImageProcessor> image_processor_;
    std::shared_ptr<Text::ITextProcessor> text_processor_;
    std::shared_ptr<Sandbox::ISandbox> sandbox_;
    mutable std::mutex agents_mutex_;

    // HTTP transport state (content/network).
    struct PendingRoute {
        std::string method;
        std::string pattern;
        H::HttpHandler handler;
    };
    std::vector<PendingRoute> custom_routes_;
    std::function<bool(const H::HttpRequest&)> auth_validator_;
    mutable std::mutex transport_mutex_;
    std::unique_ptr<H::HttpServer> http_server_;
    std::thread http_thread_;
};

OpenAIServer::OpenAIServer(const ServerConfig& config)
    : impl_(std::make_unique<Impl>(config)) {
}

OpenAIServer::~OpenAIServer() {
    Stop();
    // HttpServer::Run detaches one thread per connection which captures the
    // raw HttpServer pointer; keep-alive sockets can outlive Stop(). Leak the
    // server object rather than risk a use-after-free on those threads.
    if (impl_->http_server_) {
        impl_->http_server_.release();
    }
}

// Wraps a route handler with CORS headers, the built-in token check, and the
// custom auth validator. Runs on the HttpServer worker thread.
H::HttpHandler OpenAIServer::WrapRoute(H::HttpHandler handler) {
    return [this, handler = std::move(handler)](
               const H::HttpRequest& req,
               const std::map<std::string, std::string>& params)
               -> H::HttpResponse {
        const bool cors = impl_->config_.enable_cors;
        auto add_cors = [&req, cors](H::HttpResponse& r) {
            if (!cors) return;
            r.headers.Set("Access-Control-Allow-Origin", "*");
            r.headers.Set("Access-Control-Allow-Methods",
                          "GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS");
            r.headers.Set("Access-Control-Allow-Headers",
                          "Content-Type, Authorization, x-api-key");
        };

        // CORS preflight short-circuits before auth.
        if (cors && req.method == H::HttpMethod::kOptions) {
            H::HttpResponse r;
            r.status = 204;
            add_cors(r);
            return r;
        }

        bool authorized = true;
        if (impl_->config_.enable_auth &&
            !impl_->config_.auth_token.empty()) {
            const std::string auth = req.headers.Get("authorization");
            authorized =
                (auth.size() > 7 && auth.substr(0, 7) == "Bearer " &&
                 auth.substr(7) == impl_->config_.auth_token) ||
                req.headers.Get("x-api-key") == impl_->config_.auth_token;
        }
        if (authorized) {
            std::function<bool(const H::HttpRequest&)> validator;
            {
                std::lock_guard<std::mutex> lk(impl_->transport_mutex_);
                validator = impl_->auth_validator_;
            }
            if (validator && !validator(req)) authorized = false;
        }
        if (!authorized) {
            H::HttpResponse r =
                H::HttpResponse::JsonError(401, "unauthorized");
            add_cors(r);
            return r;
        }

        H::HttpResponse resp = handler(req, params);
        add_cors(resp);
        return resp;
    };
}

// Maps an HTTP request onto a ServerRequest, dispatches through
// HandleRequest, and converts the ServerResponse back to HTTP.
H::HttpResponse OpenAIServer::DispatchHttp(
    const H::HttpRequest& req, RequestType type) {
    ServerRequest sreq;
    sreq.type = type;
    sreq.request_id = NextRequestId();
    sreq.client_id = req.remote;
    sreq.headers = FlattenHeaders(req.headers);
    sreq.body = req.body;
    sreq.parameters = ParseParameters(req.body);

    ServerResponse sresp = HandleRequest(sreq).get();

    H::HttpResponse resp;
    resp.status = sresp.status_code;
    resp.body = sresp.body;
    for (const auto& [name, value] : sresp.headers) {
        resp.headers.Set(name, value);
    }
    if (!sresp.content_type.empty()) {
        resp.headers.Set("Content-Type", sresp.content_type);
    }
    return resp;
}

bool OpenAIServer::Start() {
    if (impl_->is_running_) {
        return false;
    }

    impl_->is_running_ = true;
    impl_->stats_.start_time = std::chrono::system_clock::now();

    auto router = std::make_shared<H::HttpRouter>();

    // Custom routes first — first match wins in HttpRouter, so embedders
    // can shadow the built-in OpenAI endpoints with their own handlers.
    {
        std::lock_guard<std::mutex> lk(impl_->transport_mutex_);
        for (const auto& route : impl_->custom_routes_) {
            router->AddRoute(
                H::HttpMethodFromString(route.method), route.pattern,
                WrapRoute(route.handler));
        }
    }

    auto dispatch = [this](RequestType type) {
        return [this, type](const H::HttpRequest& req,
                            const std::map<std::string, std::string>&)
                   -> H::HttpResponse { return DispatchHttp(req, type); };
    };
    router->AddRoute("POST", "/v1/chat/completions",
                     WrapRoute(dispatch(RequestType::kChatCompletion)));
    router->AddRoute("POST", "/v1/completions",
                     WrapRoute(dispatch(RequestType::kChatCompletion)));
    router->AddRoute("POST", "/v1/images/generations",
                     WrapRoute(dispatch(RequestType::kImageGeneration)));
    router->AddRoute("POST", "/v1/images/edits",
                     WrapRoute(dispatch(RequestType::kImageGeneration)));
    router->AddRoute("POST", "/v1/audio/transcriptions",
                     WrapRoute(dispatch(RequestType::kAudioTranscription)));
    router->AddRoute("POST", "/v1/audio/speech",
                     WrapRoute(dispatch(RequestType::kAudioTranscription)));
    // Minimal OpenAI-compat liveness endpoint for client probes.
    router->AddRoute(
        "GET", "/v1/models",
        WrapRoute([](const H::HttpRequest&,
                     const std::map<std::string, std::string>&) {
            return H::HttpResponse::Json(
                "{\"object\":\"list\",\"data\":[{\"id\":\"xer-local\","
                "\"object\":\"model\",\"created\":0,\"owned_by\":\"xer\"}]}");
        }));

    impl_->http_server_ = std::make_unique<H::HttpServer>();
    impl_->http_server_->SetRouter(router);
    impl_->http_server_->SetMaxBodyBytes(64ull * 1024 * 1024);

    std::string error;
    if (!impl_->http_server_->Listen(impl_->config_.host,
                                     static_cast<uint16_t>(impl_->config_.port),
                                     impl_->config_.max_connections, &error)) {
        impl_->is_running_ = false;
        impl_->http_server_.reset();
        return false;
    }

    impl_->http_thread_ = std::thread([this] { impl_->http_server_->Run(); });
    return true;
}

void OpenAIServer::Stop() {
    if (!impl_->is_running_) {
        return;
    }
    impl_->is_running_ = false;
    if (impl_->http_server_) {
        impl_->http_server_->Stop();
    }
    if (impl_->http_thread_.joinable()) {
        impl_->http_thread_.join();
    }
    impl_->http_thread_ = std::thread();
}

bool OpenAIServer::IsRunning() const {
    return impl_->is_running_ && impl_->http_server_ &&
           impl_->http_server_->IsRunning();
}

bool OpenAIServer::AddRoute(const std::string& method,
                            const std::string& pattern,
                            H::HttpHandler handler) {
    if (impl_->is_running_) return false;
    std::lock_guard<std::mutex> lk(impl_->transport_mutex_);
    impl_->custom_routes_.push_back({method, pattern, std::move(handler)});
    return true;
}

void OpenAIServer::SetAuthValidator(
    std::function<bool(const H::HttpRequest&)> validator) {
    std::lock_guard<std::mutex> lk(impl_->transport_mutex_);
    impl_->auth_validator_ = std::move(validator);
}

void OpenAIServer::SetConfig(const ServerConfig& config) {
    impl_->config_ = config;
}

ServerConfig OpenAIServer::GetConfig() const {
    return impl_->config_;
}

ServerStats OpenAIServer::GetStats() const {
    return impl_->stats_;
}

void OpenAIServer::ResetStats() {
    impl_->stats_.total_requests = 0;
    impl_->stats_.successful_requests = 0;
    impl_->stats_.failed_requests = 0;
    impl_->stats_.active_connections = 0;
    impl_->stats_.start_time = std::chrono::system_clock::now();
}

void OpenAIServer::SetClient(std::shared_ptr<Client::OpenAIClient> client) {
    impl_->client_ = client;
}

void OpenAIServer::RegisterAgent(const std::string& name, std::shared_ptr<Agent::IAgent> agent) {
    std::lock_guard<std::mutex> lock(impl_->agents_mutex_);
    impl_->agents_[name] = agent;
}

void OpenAIServer::UnregisterAgent(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->agents_mutex_);
    impl_->agents_.erase(name);
}

void OpenAIServer::SetImageProcessor(std::shared_ptr<Image::IImageProcessor> processor) {
    impl_->image_processor_ = processor;
}

void OpenAIServer::SetTextProcessor(std::shared_ptr<Text::ITextProcessor> processor) {
    impl_->text_processor_ = processor;
}

void OpenAIServer::SetSandbox(std::shared_ptr<Sandbox::ISandbox> sandbox) {
    impl_->sandbox_ = sandbox;
}

std::future<ServerResponse> OpenAIServer::HandleRequest(const ServerRequest& request) {
    std::promise<ServerResponse> promise;
    auto future = promise.get_future();

    impl_->thread_pool_.Enqueue([this, request, promise = std::move(promise)]() mutable {
        impl_->stats_.total_requests++;
        impl_->stats_.active_connections++;

        ServerResponse response;
        response.request_id = request.request_id;

        try {
            switch (request.type) {
                case RequestType::kChatCompletion:
                    response = HandleChatCompletion(request);
                    break;
                case RequestType::kImageGeneration:
                    response = HandleImageGeneration(request);
                    break;
                case RequestType::kAudioTranscription:
                    response = HandleAudioTranscription(request);
                    break;
                case RequestType::kTextProcessing:
                    response = HandleTextProcessing(request);
                    break;
                case RequestType::kAgentProcess:
                    response = HandleAgentProcess(request);
                    break;
                case RequestType::kSkillExecute:
                    response = HandleSkillExecute(request);
                    break;
                case RequestType::kSandboxExecute:
                    response = HandleSandboxExecute(request);
                    break;
                default:
                    response.status_code = 400;
                    response.success = false;
                    response.error_message = "Unknown request type";
                    break;
            }

            if (response.success) {
                impl_->stats_.successful_requests++;
            } else {
                impl_->stats_.failed_requests++;
            }
        } catch (const std::exception& e) {
            response.status_code = 500;
            response.success = false;
            response.error_message = e.what();
            impl_->stats_.failed_requests++;
        }

        impl_->stats_.active_connections--;
        promise.set_value(response);
    });

    return future;
}

ServerResponse OpenAIServer::HandleChatCompletion(const ServerRequest& request) {
    (void)request;
    ServerResponse response;

    if (!impl_->client_) {
        response.status_code = 503;
        response.success = false;
        response.error_message = "OpenAI client not configured";
        return response;
    }

    // TODO: Parse request body for messages and options
    std::vector<ChatMessage> messages;
    CompletionOptions options;

    auto completion = impl_->client_->CreateChatCompletion(messages, options).get();

    std::stringstream ss;
    ss << "{\"id\":\"" << completion.id << "\","
       << "\"model\":\"" << completion.model << "\","
       << "\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\""
       << (completion.choices.empty() ? "" : completion.choices[0].content[0].text)
       << "\"}}],"
       << "\"usage\":{\"prompt_tokens\":" << completion.usage.prompt_tokens
       << ",\"completion_tokens\":" << completion.usage.completion_tokens
       << ",\"total_tokens\":" << completion.usage.total_tokens << "}}";

    response.body = ss.str();
    response.content_type = "application/json";
    response.status_code = 200;
    response.success = true;

    return response;
}

ServerResponse OpenAIServer::HandleImageGeneration(const ServerRequest& request) {
    (void)request;
    ServerResponse response;

    if (!impl_->image_processor_) {
        response.status_code = 503;
        response.success = false;
        response.error_message = "Image processor not configured";
        return response;
    }

    // TODO: Parse request body for image generation parameters
    ImageGenerationParams params;

    auto image_response = impl_->image_processor_->GenerateImage(params).get();

    std::stringstream ss;
    ss << "{\"created\":" << std::time(nullptr) << ","
       << "\"data\":[{\"url\":\"" << image_response.url << "\""
       << ",\"revised_prompt\":\"" << image_response.revised_prompt << "\"}]}";

    response.body = ss.str();
    response.content_type = "application/json";
    response.status_code = 200;
    response.success = true;

    return response;
}

ServerResponse OpenAIServer::HandleAudioTranscription(const ServerRequest& request) {
    (void)request;
    ServerResponse response;

    if (!impl_->client_) {
        response.status_code = 503;
        response.success = false;
        response.error_message = "OpenAI client not configured";
        return response;
    }

    // TODO: Parse request body for audio transcription parameters
    AudioTranscriptionParams params;

    auto transcription = impl_->client_->TranscribeAudio(params).get();

    std::stringstream ss;
    ss << "{\"text\":\"" << transcription.text << "\","
       << "\"language\":\"" << transcription.language << "\","
       << "\"duration\":" << transcription.duration << "}";

    response.body = ss.str();
    response.content_type = "application/json";
    response.status_code = 200;
    response.success = true;

    return response;
}

ServerResponse OpenAIServer::HandleTextProcessing(const ServerRequest& request) {
    (void)request;
    ServerResponse response;

    if (!impl_->text_processor_) {
        response.status_code = 503;
        response.success = false;
        response.error_message = "Text processor not configured";
        return response;
    }

    // TODO: Parse request body for text processing parameters
    Text::TextProcessingParams params;

    auto result = impl_->text_processor_->ProcessText(params).get();

    std::stringstream ss;
    ss << "{\"processed_text\":\"" << result.processed_text << "\","
       << "\"word_count\":" << result.word_count << ","
       << "\"sentence_count\":" << result.sentence_count << ","
       << "\"sentiment_score\":" << result.sentiment_score << ","
       << "\"sentiment_label\":\"" << result.sentiment_label << "\"}";

    response.body = ss.str();
    response.content_type = "application/json";
    response.status_code = 200;
    response.success = true;

    return response;
}

ServerResponse OpenAIServer::HandleAgentProcess(const ServerRequest& request) {
    ServerResponse response;

    std::lock_guard<std::mutex> lock(impl_->agents_mutex_);

    auto agent_name_it = request.parameters.find("agent");
    if (agent_name_it == request.parameters.end()) {
        response.status_code = 400;
        response.success = false;
        response.error_message = "Agent name not specified";
        return response;
    }

    auto agent_it = impl_->agents_.find(agent_name_it->second);
    if (agent_it == impl_->agents_.end()) {
        response.status_code = 404;
        response.success = false;
        response.error_message = "Agent not found: " + agent_name_it->second;
        return response;
    }

    // TODO: Parse request body for input
    std::string input = "Sample input";

    auto completion = agent_it->second->Process(input).get();

    std::stringstream ss;
    ss << "{\"agent\":\"" << agent_name_it->second << "\","
       << "\"response\":\""
       << (completion.choices.empty() ? "" : completion.choices[0].content[0].text)
       << "\","
       << "\"state\":\"idle\"}";

    response.body = ss.str();
    response.content_type = "application/json";
    response.status_code = 200;
    response.success = true;

    return response;
}

ServerResponse OpenAIServer::HandleSkillExecute(const ServerRequest& request) {
    ServerResponse response;

    auto skill_name_it = request.parameters.find("skill");
    if (skill_name_it == request.parameters.end()) {
        response.status_code = 400;
        response.success = false;
        response.error_message = "Skill name not specified";
        return response;
    }

    auto skill = Skills::SkillRegistry::GetInstance().GetSkill(skill_name_it->second);
    if (!skill) {
        response.status_code = 404;
        response.success = false;
        response.error_message = "Skill not found: " + skill_name_it->second;
        return response;
    }

    // TODO: Parse request body for parameters
    std::map<std::string, std::string> params;

    auto result = skill->Execute(params).get();

    std::stringstream ss;
    ss << "{\"skill\":\"" << skill_name_it->second << "\","
       << "\"success\":" << (result.success ? "true" : "false") << ","
       << "\"output\":\"" << result.output << "\","
       << "\"execution_time_ms\":" << result.execution_time_ms << "}";

    response.body = ss.str();
    response.content_type = "application/json";
    response.status_code = 200;
    response.success = true;

    return response;
}

ServerResponse OpenAIServer::HandleSandboxExecute(const ServerRequest& request) {
    (void)request;
    ServerResponse response;

    if (!impl_->sandbox_) {
        response.status_code = 503;
        response.success = false;
        response.error_message = "Sandbox not configured";
        return response;
    }

    // TODO: Parse request body for execution context
    Sandbox::ExecutionContext context;

    auto result = impl_->sandbox_->ExecuteCode(context).get();

    std::stringstream ss;
    ss << "{\"success\":" << (result.success ? "true" : "false") << ","
       << "\"exit_code\":" << result.exit_code << ","
       << "\"stdout\":\"" << result.stdout_output << "\","
       << "\"stderr\":\"" << result.stderr_output << "\","
       << "\"execution_time_ms\":" << result.execution_time_ms << ","
       << "\"memory_used_mb\":" << result.memory_used_mb << "}";

    response.body = ss.str();
    response.content_type = "application/json";
    response.status_code = 200;
    response.success = true;

    return response;
}

// ServerFactory implementation
std::unique_ptr<IOpenAIServer> ServerFactory::CreateServer(
    const ServerConfig& config) {

    return std::make_unique<OpenAIServer>(config);
}

std::unique_ptr<IOpenAIServer> ServerFactory::CreateDefaultServer() {
    return CreateServer(ServerConfig());
}

// AuthMiddleware implementation
AuthMiddleware::AuthMiddleware(const std::string& token)
    : auth_token_(token) {
}

bool AuthMiddleware::Process(const ServerRequest& request, ServerResponse& response) {
    auto auth_it = request.headers.find("Authorization");
    if (auth_it == request.headers.end()) {
        response.status_code = 401;
        response.success = false;
        response.error_message = "Missing Authorization header";
        return false;
    }

    if (auth_it->second != "Bearer " + auth_token_) {
        response.status_code = 403;
        response.success = false;
        response.error_message = "Invalid authentication token";
        return false;
    }

    return true;
}

// RateLimitMiddleware implementation
RateLimitMiddleware::RateLimitMiddleware(int requests_per_minute)
    : rate_limit_(requests_per_minute) {
}

bool RateLimitMiddleware::Process(const ServerRequest& request, ServerResponse& response) {
    auto now = std::chrono::system_clock::now();
    auto client_id = request.client_id;

    std::lock_guard<std::mutex> lock(mutex_);

    auto& history = request_history_[client_id];

    // Remove requests older than 1 minute
    auto one_minute_ago = now - std::chrono::minutes(1);
    history.erase(
        std::remove_if(history.begin(), history.end(),
            [one_minute_ago](const auto& timestamp) {
                return timestamp < one_minute_ago;
            }
        ),
        history.end()
    );

    if (static_cast<int>(history.size()) >= rate_limit_) {
        response.status_code = 429;
        response.success = false;
        response.error_message = "Rate limit exceeded";
        return false;
    }

    history.push_back(now);
    return true;
}

// LoggingMiddleware implementation
LoggingMiddleware::LoggingMiddleware(const std::string& log_level)
    : log_level_(log_level) {
}

bool LoggingMiddleware::Process(const ServerRequest& request, ServerResponse& response) {
    (void)request;
    (void)response;
    // TODO: Implement actual logging
    // For now, just allow request to pass through
    return true;
}

}  // namespace Content::OpenAI::Server
