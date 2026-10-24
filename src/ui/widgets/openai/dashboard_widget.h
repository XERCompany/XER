#ifndef XER_UI_WIDGETS_OPENAI_DASHBOARD_WIDGET_H
#define XER_UI_WIDGETS_OPENAI_DASHBOARD_WIDGET_H

#include "../../widgets/widget.h"
#include "../../../content/openai/openai.h"
#include <string>
#include <memory>
#include <functional>

namespace XER::UI::Widgets::OpenAI {

/**
 * DashboardTab - Tab identifier for dashboard
 */
enum class DashboardTab {
    kChat,
    kImage,
    kAudio,
    kAgent,
    kSettings
};

/**
 * OpenAIDashboardWidget - Main dashboard for OpenAI features
 *
 * Provides:
 * - Tabbed interface (Chat, Image, Audio, Agent, Settings)
 * - Usage statistics display
 * - API key management
 * - System status indicators
 * - Quick actions
 * - Model selection
 */
class OpenAIDashboardWidget : public Widget {
public:
    OpenAIDashboardWidget();
    ~OpenAIDashboardWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem);
    
    // Tab management
    void SetActiveTab(DashboardTab tab);
    DashboardTab GetActiveTab() const;
    
    // Statistics
    void UpdateUsageStats();
    
    // Configuration
    void SetAPIKey(const std::string& api_key);
    std::string GetAPIKey() const;
    
    // Quick actions
    void QuickChat(const std::string& message);
    void QuickImageGenerate(const std::string& prompt);
    
    // Callbacks
    using TabChangeCallback = std::function<void(DashboardTab)>;
    void OnTabChange(TabChangeCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * UsageStatsWidget - Display API usage statistics
 */
class UsageStatsWidget : public Widget {
public:
    UsageStatsWidget();
    ~UsageStatsWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetStats(const Content::OpenAI::Client::UsageStats& stats);
    void SetRateLimit(int requests_per_minute);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * SystemStatusWidget - Display system status
 */
class SystemStatusWidget : public Widget {
public:
    SystemStatusWidget();
    ~SystemStatusWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetAPIConnected(bool connected);
    void SetServerRunning(bool running);
    void SetAgentActive(const std::string& agent_name, bool active);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Widgets::OpenAI

#endif // XER_UI_WIDGETS_OPENAI_DASHBOARD_WIDGET_H
