// ControllAccountsWindows — management UI for XER SecurityFlashService.
//
// Thin host around SecurityFlashPanelWidget: service install/uninstall/
// start/stop, drive + token status polling. All logic lives in the
// widget (src/ui/widgets/security_flash_panel_widget.*).

#include <memory>

#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/widgets/security_flash_panel_widget.h"

int main() {
	auto& ui_app = XER::UI::Core::Application::Instance();
	ui_app.SetAppName("ControllAccountsWindows");
	if (!ui_app.Initialize()) {
		return 1;
	}

	auto window = std::make_shared<XER::UI::Core::Window>();
	// The panel needs ~570 logical px of height; with per-monitor DPI
	// awareness the client area shrinks by 96/dpi, so leave headroom.
	if (!window->Create("SecurityFlash — Account Control", 820, 680)) {
		return 1;
	}
	window->SetSizeLimits(720, 580, 0, 0);

	auto panel =
	    std::make_shared<XER::UI::Widgets::SecurityFlashPanelWidget>();
	window->SetContent(panel);

	// Drive the widget tree tick (buttons' spinners + status polling)
	// at ~10 Hz; widgets invalidate only on real changes.
	window->SetFrameCallback(
	    [panel]() { panel->TickTree(0.1); }, 100);

	window->Show();
	return ui_app.Run();
}
