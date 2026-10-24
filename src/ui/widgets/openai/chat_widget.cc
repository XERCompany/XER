#include "chat_widget.h"
#include "../../../../async_io/io_thread_pool.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace XER::UI::Widgets::OpenAI {

class ChatWidget::Impl {
public:
    Impl()
        : subsystem_(nullptr),
          thread_pool_(IO::AsyncIO::IOThreadPool::GetSharedInstance()),
          selected_agent_("default"),
          model_("gpt-4"),
          temperature_(0.7f),
          max_tokens_(2048),
          streaming_enabled_(true),
          user_color_("#E2E8F0"),
          assistant_color_("#F1F5F9"),
          system_color_("#94A3B8") {
    }

    std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem_;
    IO::AsyncIO::IOThreadPool& thread_pool_;
    
    std::string selected_agent_;
    std::string model_;
    float temperature_;
    int max_tokens_;
    bool streaming_enabled_;
    
    std::string user_color_;
    std::string assistant_color_;
    std::string system_color_;
    
    std::vector<ChatMessageUI> messages_;
    std::string current_input_;
    
    MessageCallback message_callback_;
    mutable std::mutex messages_mutex_;
};

ChatWidget::ChatWidget()
    : impl_(std::make_unique<Impl>()) {
}

void ChatWidget::Render(Rendering::Renderer& renderer) {
    RenderMessages(renderer);
    RenderInputArea(renderer);
    RenderTokenInfo(renderer);
}

void ChatWidget::Layout(const Layout::Rect& bounds) {
    // TODO: Implement layout calculation
}

bool ChatWidget::HandleEvent(const Event& event) {
    // TODO: Implement event handling
    return false;
}

Layout::Size ChatWidget::GetMinSize() const {
    return {400, 300};
}

Layout::Size ChatWidget::GetPreferredSize() const {
    return {800, 600};
}

Layout::Size ChatWidget::GetMaxSize() const {
    return {1920, 1080};
}

void ChatWidget::SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem) {
    impl_->subsystem_ = subsystem;
}

void ChatWidget::SetAgent(const std::string& agent_name) {
    impl_->selected_agent_ = agent_name;
}

void ChatWidget::SendMessage(const std::string& message) {
    if (!impl_->subsystem_) {
        return;
    }

    // Add user message
    {
        std::lock_guard<std::mutex> lock(impl_->messages_mutex_);
        ChatMessageUI user_msg;
        user_msg.role = Content::OpenAI::MessageRole::kUser;
        user_msg.content = message;
        user_msg.timestamp = GetCurrentTimestamp();
        user_msg.is_streaming = false;
        impl_->messages_.push_back(user_msg);
    }

    // Get agent
    auto agent = impl_->subsystem_->GetAgent(impl_->selected_agent_);
    if (!agent) {
        // Use client directly if no agent
        auto client = impl_->subsystem_->GetClient();
        if (client) {
            impl_->thread_pool_.Enqueue([this, message, client]() {
                std::vector<Content::OpenAI::ChatMessage> messages;
                messages.push_back({Content::OpenAI::MessageRole::kUser,
                    {{Content::OpenAI::ContentType::kText, message}}});

                Content::OpenAI::CompletionOptions options;
                options.model = impl_->model_;
                options.temperature = impl_->temperature_;
                options.max_tokens = impl_->max_tokens_;
                options.stream = impl_->streaming_enabled_;

                if (impl_->streaming_enabled_) {
                    client->CreateChatCompletionStream(
                        messages,
                        options,
                        [this](const std::string& chunk) {
                            // Streaming callback
                            ChatMessageUI assistant_msg;
                            assistant_msg.role = Content::OpenAI::MessageRole::kAssistant;
                            assistant_msg.content = chunk;
                            assistant_msg.timestamp = GetCurrentTimestamp();
                            assistant_msg.is_streaming = true;
                            
                            if (impl_->message_callback_) {
                                impl_->message_callback_(assistant_msg);
                            }
                        },
                        [this](const Content::OpenAI::CompletionResponse& response) {
                            // Completion callback
                            if (!response.choices.empty()) {
                                ChatMessageUI final_msg;
                                final_msg.role = Content::OpenAI::MessageRole::kAssistant;
                                final_msg.content = response.choices[0].content[0].text;
                                final_msg.timestamp = GetCurrentTimestamp();
                                final_msg.is_streaming = false;
                                
                                std::lock_guard<std::mutex> lock(impl_->messages_mutex_);
                                impl_->messages_.push_back(final_msg);
                            }
                        }
                    );
                } else {
                    auto response = client->CreateChatCompletion(messages, options).get();
                    
                    if (!response.choices.empty()) {
                        ChatMessageUI assistant_msg;
                        assistant_msg.role = Content::OpenAI::MessageRole::kAssistant;
                        assistant_msg.content = response.choices[0].content[0].text;
                        assistant_msg.timestamp = GetCurrentTimestamp();
                        assistant_msg.is_streaming = false;
                        
                        std::lock_guard<std::mutex> lock(impl_->messages_mutex_);
                        impl_->messages_.push_back(assistant_msg);
                        
                        if (impl_->message_callback_) {
                            impl_->message_callback_(assistant_msg);
                        }
                    }
                }
            });
        }
    } else {
        // Use agent
        auto response = agent->Process(message).get();
        
        if (!response.choices.empty()) {
            ChatMessageUI assistant_msg;
            assistant_msg.role = Content::OpenAI::MessageRole::kAssistant;
            assistant_msg.content = response.choices[0].content[0].text;
            assistant_msg.timestamp = GetCurrentTimestamp();
            assistant_msg.is_streaming = false;
            
            std::lock_guard<std::mutex> lock(impl_->messages_mutex_);
            impl_->messages_.push_back(assistant_msg);
            
            if (impl_->message_callback_) {
                impl_->message_callback_(assistant_msg);
            }
        }
    }
}

void ChatWidget::ClearChat() {
    std::lock_guard<std::mutex> lock(impl_->messages_mutex_);
    impl_->messages_.clear();
}

void ChatWidget::SetUserMessageColor(const std::string& color) {
    impl_->user_color_ = color;
}

void ChatWidget::SetAssistantMessageColor(const std::string& color) {
    impl_->assistant_color_ = color;
}

void ChatWidget::SetSystemMessageColor(const std::string& color) {
    impl_->system_color_ = color;
}

void ChatWidget::OnMessageReceived(MessageCallback callback) {
    impl_->message_callback_ = callback;
}

void ChatWidget::SetModel(const std::string& model) {
    impl_->model_ = model;
}

void ChatWidget::SetTemperature(float temperature) {
    impl_->temperature_ = temperature;
}

void ChatWidget::SetMaxTokens(int max_tokens) {
    impl_->max_tokens_ = max_tokens;
}

void ChatWidget::EnableStreaming(bool enable) {
    impl_->streaming_enabled_ = enable;
}

void ChatWidget::RenderMessages(Rendering::Renderer& renderer) {
    // TODO: Implement actual rendering using XER rendering system
    // This would use Direct2D or the rendering abstraction layer
}

void ChatWidget::RenderInputArea(Rendering::Renderer& renderer) {
    // TODO: Implement input area rendering
}

void ChatWidget::RenderTokenInfo(Rendering::Renderer& renderer) {
    // TODO: Implement token count display
}

std::string ChatWidget::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
    return ss.str();
}

// ChatConfigWidget implementation
class ChatConfigWidget::Impl {
public:
    Impl()
        : subsystem_(nullptr),
          model_("gpt-4"),
          temperature_(0.7f),
          max_tokens_(2048),
          streaming_enabled_(true) {
    }

    std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem_;
    std::string model_;
    float temperature_;
    int max_tokens_;
    bool streaming_enabled_;
};

ChatConfigWidget::ChatConfigWidget()
    : impl_(std::make_unique<Impl>()) {
}

void ChatConfigWidget::Render(Rendering::Renderer& renderer) {
    // TODO: Implement configuration UI rendering
}

void ChatConfigWidget::Layout(const Layout::Rect& bounds) {
    // TODO: Implement layout
}

bool ChatConfigWidget::HandleEvent(const Event& event) {
    // TODO: Implement event handling
    return false;
}

Layout::Size ChatConfigWidget::GetMinSize() const {
    return {300, 200};
}

Layout::Size ChatConfigWidget::GetPreferredSize() const {
    return {400, 300};
}

Layout::Size ChatConfigWidget::GetMaxSize() const {
    return {600, 400};
}

void ChatConfigWidget::SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem) {
    impl_->subsystem_ = subsystem;
}

std::string ChatConfigWidget::GetModel() const {
    return impl_->model_;
}

float ChatConfigWidget::GetTemperature() const {
    return impl_->temperature_;
}

int ChatConfigWidget::GetMaxTokens() const {
    return impl_->max_tokens_;
}

bool ChatConfigWidget::IsStreamingEnabled() const {
    return impl_->streaming_enabled_;
}

} // namespace XER::UI::Widgets::OpenAI
