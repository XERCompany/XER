// Extensions panel: right-dock view for the .xexrj extension host
// (tools/video_editor/extensions/ext_host.h). Two modes inside one
// tab:
//
//   List — section header "EXTENSIONS" + refresh button over a
//   ScrollArea of Item rows (pack icon, extension name, version +
//   status). Row actions: a trailing "Open" button on extensions that
//   declare a UI, plus the footer Run / Reveal pair on the selection
//   (Enter opens, double-click too — the plugins-panel convention).
//
//   View — the shared CEF surface (browser_panel_widget.h) embedded
//   as a child, showing the extension's static/ page served from the
//   verified TZ archive over xer-ext://. A back row returns to the
//   list; the extension host's openUi() funnel lands here too.
//
// The panel owns the browser widget outright — the old standalone
// "Web" tab became this view, so tab index 9 keeps its slot while the
// Chromium HWND is shown/hidden with the mode switch.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "extensions/ext_host.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Item;
class Label;
class ScrollArea;
}

namespace XER::VideoEditor {
class BrowserPanelWidget;
}

namespace XER::VideoEditor::Ui {

class ExtensionsPanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Status-bar line for run/scan outcomes (mirrors LabPanel).
        std::function<void(const std::string& text, bool is_error)>
            on_status;
        // OS-shell reveal for an extension path — the app owns
        // ShellExecute so the panel stays platform-free.
        std::function<void(const std::string& path)> on_reveal;
        // Refresh button: the app rescans assets/extensions/ through
        // ExtHost::Shared().Scan() (it owns the assets dir) and then
        // calls Reload() — the panel never touches the filesystem.
        std::function<void()> on_rescan;
    };

    ExtensionsPanelWidget(void* parent_hwnd, Hooks hooks = {});
    ~ExtensionsPanelWidget() override;

    // Re-reads ExtHost::Shared().Entries() and rebuilds the list —
    // call after Scan() so rows track the on-disk set.
    void Reload();
    // Opens `id`'s declared UI in the embedded browser (verified
    // extensions with a [extension] ui member only) and flips the
    // panel to View mode.
    void OpenExtension(const std::string& id);
    // OpenUiFn entry: the extension host hands a fully-formed
    // xer-ext:// URL — show it in View mode.
    void OpenUrl(const std::string& url);
    // Back to the extension list (View mode's return affordance).
    void ShowList();

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {200.0f, 200.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {300.0f, 420.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

    // Shadow (base is non-virtual): the embedded browser's Chromium
    // HWND must hide with the panel AND with the list↔view switch —
    // the app calls through the derived pointer like it did for
    // BrowserPanelWidget.
    void SetVisible(bool visible);

private:
    enum class Mode { List, View };

    // One extension row in the list — a snapshot of the host entry so
    // a rescan can't move strings out from under a drawn row.
    struct Row {
        std::string id;
        std::string name;
        std::string version;
        std::string path;      // .xexrj on disk (Reveal target)
        bool verified = false;
        bool has_ui = false;
        std::string status;    // "verified" / first verify error
    };

    // ScrollArea content holding the Item rows + selection chrome —
    // defined in the .cc (same pattern as PluginsPanelWidget::RowList).
    class RowList;

    void EnsureChildren();
    void RebuildList();
    void SyncModeVisibility();   // children shown for the current mode
    void SetMode(Mode mode);
    void SelectRow(int index);
    void EnsureSelectionVisible();
    const Row* Selected() const;
    void RunSelected();
    void RevealSelected();
    void SetStatus(const std::string& text, bool is_error = false);

    void* parent_hwnd_ = nullptr;
    Hooks hooks_;
    std::vector<Row> rows_;
    int selected_ = -1;
    Mode mode_ = Mode::List;
    std::string view_title_;            // extension name in View mode
    bool children_attached_ = false;

    // ---- list mode ----
    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Button> refresh_btn_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> scroll_area_;
    std::shared_ptr<RowList> list_;
    std::shared_ptr<XER::UI::Widgets::Button> run_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> reveal_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;

    // ---- view mode ----
    std::shared_ptr<XER::UI::Widgets::Button> back_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> view_label_;
    std::shared_ptr<BrowserPanelWidget> browser_;
};

}  // namespace XER::VideoEditor::Ui
