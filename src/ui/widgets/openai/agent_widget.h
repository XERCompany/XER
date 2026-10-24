#ifndef XER_UI_WIDGETS_OPENAI_AGENT_WIDGET_H
#define XER_UI_WIDGETS_OPENAI_AGENT_WIDGET_H

#include "../../widgets/widget.h"
#include "../../../content/openai/openai.h"
#include <string>
#include <vector>
#include <functional>

namespace XER::UI::Widgets::OpenAI {

/**
 * AgentMemoryEntryUI - UI representation of agent memory
 */
struct AgentMemoryEntryUI {
    std::string content;
    std::string timestamp;
    float relevance_score;
};

/**
 * AgentWidget - UI widget for agent interaction
 *
 * Provides:
 * - Agent selection dropdown
 * - Memory view with relevance
 * - Tool invocation display
 * - Agent state indicator
 * - Memory management (clear, export)
 * - Real-time thinking status
 */
class AgentWidget : public Widget {
public:
    AgentWidget();
    ~AgentWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem);
    
    void RegisterAgent(const std::string& name, std::shared_ptr<Content::OpenAI::Agent::IAgent> agent);
    void SelectAgent(const std::string& name);
    
    void ProcessInput(const std::string& input);
    void ClearMemory();
    void ExportMemory(const std::string& path);
    
    // State display
    void SetThinkingState(bool thinking);
    void SetExecutingState(bool executing);
    
    // Callbacks
    using AgentResponseCallback = std::function<void(const std::string& response)>;
    void OnAgentResponse(AgentResponseCallback callback);
    
    using ToolInvokeCallback = std::function<void(const std::string& tool_name, const std::string& args)>;
    void OnToolInvoke(ToolInvokeCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * AgentConfigWidget - Configuration widget for agent settings
 */
class AgentConfigWidget : public Widget {
public:
    AgentConfigWidget();
    ~AgentConfigWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem);
    void SetAgent(const std::string& agent_name);

    // Get configuration
    std::string GetSystemPrompt() const;
    float GetTemperature() const;
    int GetMaxTokens() const;
    bool IsMemoryEnabled() const;
    bool AreToolsEnabled() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Widgets::OpenAI

#endif // XER_UI_WIDGETS_OPENAI_AGENT_WIDGET_H
