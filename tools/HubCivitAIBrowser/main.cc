// HubCivitAIBrowser.exe — Civitai Studio Hub (search, filters, generated
// artwork previews, Download + ArchiveManager ZIP inspector dialog, Downloads
// & Settings workspace tabs) built on src/ui + XER Cache/AsyncIO/Archive/Network.

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
    if (!window->Create("Hub Civitai Browser", 1320, 840)) {
        return 1;
    }

    auto browser =
        std::make_shared<XER::Tools::HubBrowser::HubBrowserWidget>();
    window->SetContent(browser);
    window->SetOnClose([]() {});

    window->SetCustomTitleBar(true, 38.0f);
    window->SetTitleBarHitTest([browser](XER::UI::Layout::Point p) {
        auto* tb = browser->TitleBar();
        return tb != nullptr && tb->IsInteractiveAt(p);
    });
    window->SetTheme(XER::UI::Theme::Variant::Dark);
    window->SetSizeLimits(820, 560, 0, 0);

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

    auto apply_theme = [window, tb](bool dark) {
        window->SetTheme(dark ? XER::UI::Theme::Variant::Dark
                              : XER::UI::Theme::Variant::Light);
        tb->SetThemeToggle(dark, [window](bool next_dark) {
            window->SetTheme(next_dark ? XER::UI::Theme::Variant::Dark
                                       : XER::UI::Theme::Variant::Light);
        });
    };
    browser->SetOnThemeRequest(apply_theme);

    auto sync_maximize = [window, tb]() {
        tb->SetMaximized(window->IsWindowMaximized());
    };
    window->SetOnResize([sync_maximize](int, int) { sync_maximize(); });

    window->SetFrameCallback(
        [browser, sync_maximize]() {
            browser->TickTree(0.016);
            sync_maximize();
        },
        66);

    window->Show();
    return app.Run();
}
