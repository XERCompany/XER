#ifndef XER_UI_WIDGETS_SECURITY_FLASH_PANEL_WIDGET_H
#define XER_UI_WIDGETS_SECURITY_FLASH_PANEL_WIDGET_H

#include <memory>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Button;
class Label;
class Textarea;

// Management panel for the XER SecurityFlashService Windows service.
//
// Shows the service state (not installed / stopped / running), offers
// Install / Uninstall / Start / Stop / Refresh actions and lists every
// mounted removable drive with its SecurityFlash token status.
//
// Install/Uninstall are delegated to the sibling
// SecurityFlashService.exe (--install / --uninstall), which performs
// its own UAC elevation. Start/Stop go through the SCM directly and
// require an elevated host for non-admin service DACLs.
//
// Status is polled from Tick() (driven by Window::SetFrameCallback);
// labels only invalidate when the text actually changes.
class SecurityFlashPanelWidget final : public Widget {
public:
    SecurityFlashPanelWidget();
    ~SecurityFlashPanelWidget() override = default;

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Layout(const Layout::Rect& bounds,
                const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void Tick(double dt_seconds) override;

private:
    void EnsureChildrenAttached();
    void RefreshStatus();
    void SetActionResult(const std::string& text);

    void OnInstall();
    void OnUninstall();
    void OnStart();
    void OnStop();
    void OnRefresh();

    std::shared_ptr<Label> service_label_;
    std::shared_ptr<Label> action_label_;
    std::shared_ptr<Button> install_button_;
    std::shared_ptr<Button> uninstall_button_;
    std::shared_ptr<Button> start_button_;
    std::shared_ptr<Button> stop_button_;
    std::shared_ptr<Button> refresh_button_;
    std::shared_ptr<Textarea> drives_area_;

    bool children_attached_ = false;
    double poll_accumulator_ = 0.0;
    std::string last_service_state_;
    std::string last_drives_text_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SECURITY_FLASH_PANEL_WIDGET_H
