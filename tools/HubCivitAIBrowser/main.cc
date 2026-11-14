// HubCivitAIBrowser.exe — Civitai model browser with search and filters.
// GUI built on the project's src/ui framework; downloads go through the
// existing XER CivitaiModelDownloader (cache + resume + sha256 verify).
// Frameless IDE-style chrome via Window::SetCustomTitleBar + AppTitleBar.

#include "hub_browser_widget.h"

#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/theme/theme.h"
#include "ui/widgets/app_title_bar.h"

#include <memory>

int main() {
    auto& app = XER::UI::Core::Application::Instance();
    app.SetAppName("HubCivitAIBrowser");
    if (!app.Initialize()) {
        return 1;
    }

    auto window = std::make_shared<XER::UI::Core::Window>();
    if (!window->Create("Hub Civitai Browser", 1280, 800)) {
        return 1;
    }

    auto browser =
        std::make_shared<XER::Tools::HubBrowser::HubBrowserWidget>();
    window->SetContent(browser);
    window->SetOnClose([]() {});

    // Frameless IDE chrome: custom titlebar height + interactive-region
    // hit-test so empty bar areas behave as native caption drag regions.
    window->SetCustomTitleBar(true, 38.0f);
    window->SetTitleBarHitTest([browser](XER::UI::Layout::Point p) {
        auto* tb = browser->TitleBar();
        return tb != nullptr && tb->IsInteractiveAt(p);
    });
    window->SetTheme(XER::UI::Theme::Variant::Dark);
    window->SetSizeLimits(760, 520, 0, 0);

    // Wire titlebar -> window commands and theme switching.
    auto* tb = browser->TitleBar();
    using WindowCommand = XER::UI::Widgets::AppTitleBar::WindowCommand;
    tb->SetOnWindowCommand([window](WindowCommand cmd) {
        switch (cmd) {
            case WindowCommand::Minimize:
                window->Minimize();
                break;
            case WindowCommand::ToggleMaximize:
                window->ToggleMaximize();
                break;
            case WindowCommand::Close:
                window->Close();
                break;
        }
    });
    tb->SetThemeToggle(
        window->GetTheme() == XER::UI::Theme::Variant::Dark,
        [window](bool dark) {
            window->SetTheme(dark ? XER::UI::Theme::Variant::Dark
                                  : XER::UI::Theme::Variant::Light);
        });

    // Keep the maximize/restore glyph in sync with actual window state.
    auto sync_maximize = [window, tb]() {
        tb->SetMaximized(window->IsWindowMaximized());
    };
    window->SetOnResize([sync_maximize](int, int) { sync_maximize(); });

    // Drive async search/download completion polling + titlebar animation.
    window->SetFrameCallback(
        [browser, sync_maximize]() {
            browser->TickTree(0.016);
            sync_maximize();
        },
        100);

    window->Show();
    return app.Run();
}
