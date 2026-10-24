#ifndef XER_UI_WIDGETS_OPENAI_CHAT_WIDGET_H
#define XER_UI_WIDGETS_OPENAI_CHAT_WIDGET_H

#include "../../widgets/widget.h"
#include "../../../content/openai/openai.h"
#include <string>
#include <vector>
#include <functional>

namespace XER::UI::Widgets::OpenAI {

/**
 * ChatMessage - UI representation of a chat message
 */
struct ChatMessageUI {
    Content::OpenAI::MessageRole role;
    std::string content;
    std::string timestamp;
    bool is_streaming;
};

/**
 * ChatWidget - UI widget for OpenAI chat interface
 *
 * Provides:
 * - Message display with role-based styling
 * - Input field for user messages
 * - Send button
 * - Streaming response support
 * - History navigation
 * - Token count display
 */
class ChatWidget : public Widget {
public:
    ChatWidget();
    ~ChatWidget() override = default;

    // Widget interface
    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    // Chat-specific operations
    void SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem);
    void SetAgent(const std::string& agent_name);
    
    void SendMessage(const std::string& message);
    void ClearChat();
    
    // Styling
    void SetUserMessageColor(const std::string& color);
    void SetAssistantMessageColor(const std::string& color);
    void SetSystemMessageColor(const std::string& color);
    
    // Callbacks
    using MessageCallback = std::function<void(const ChatMessageUI&)>;
    void OnMessageReceived(MessageCallback callback);
    
    // Configuration
    void SetModel(const std::string& model);
    void SetTemperature(float temperature);
    void SetMaxTokens(int max_tokens);
    void EnableStreaming(bool enable);

protected:
    void RenderMessages(Rendering::Renderer& renderer);
    void RenderInputArea(Rendering::Renderer& renderer);
    void RenderTokenInfo(Rendering::Renderer& renderer);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * ChatConfigWidget - Configuration widget for chat settings
 */
class ChatConfigWidget : public Widget {
public:
    ChatConfigWidget();
    ~ChatConfigWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem);

    // Get current configuration
    std::string GetModel() const;
    float GetTemperature() const;
    int GetMaxTokens() const;
    bool IsStreamingEnabled() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Widgets::OpenAI

#endif // XER_UI_WIDGETS_OPENAI_CHAT_WIDGET_H
