// ExtensionsPanel — the M3 wave-2 extensions dock (spec §6 "Ext"
// tab): the .xexrj extension-host browser rebuilt on the shared kit.
//
//   EXTENSIONS                                   [Reload]
//   ┌──────────────────────────────────────────────┐
//   │ beatcutter v1.2 · verified  [Switch][⚙][Run] │
//   │ captionist v0.9 · verified  [Switch][⚙][UI]  │
//   └──────────────────────────────────────────────┘
//   6 extensions · 5 enabled
//
// Real vs panel-level seams (the honest split — ext_host.h):
//   * REAL: enumerate() → ExtHost::Shared().Scan + Entries()
//     (id/name/version/verified/has_ui/has_native/member lists);
//     on_run → ExtHost::Shared().Run(id); on_open_ui → the
//     xer-ext:// URL hand-off; on_rescan → directory rescan.
//   * PANEL/SHELL STATE: the enabled Switch — ExtHost has no
//     enable/disable concept, so VeApp keeps a session-level
//     disabled set and gates Run/Open on it. The row flag is
//     documented here, not faked into the manifest.
//   * Settings gear → manifest details Dialog (content widget is
//     drawn text — a real settings schema doesn't exist per
//     extension; the dialog surfaces the manifest facts).
//
// Rows are per-row composite widgets inside a ScrollArea (the
// extension list is small — VirtualList's draw-only rows can't host
// a Switch child), matching the legacy RowList pattern.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Dialog;
class Label;
class ScrollArea;
class Switch;
}

namespace XER::VideoEditor::Ui2 {

// Snapshot of one ExtHost entry plus panel/shell state. The
// manifest lists (requires/native_*/errors) feed the settings
// dialog's detail view.
struct ExtensionRow {
    std::string id;
    std::string name;
    std::string version;
    std::string path;          // .xexrj on disk — the Reveal target
    bool verified = false;
    bool has_native = false;
    bool has_ui = false;
    bool enabled = true;       // panel/shell state — see the header
    std::string status;        // "verified" / first verify error
    std::string entry;
    std::string ui_member;
    std::vector<std::string> depends;   // manifest "requires" list
                                        // (renamed — `requires` is a
                                        // C++20 keyword)
    std::vector<std::string> native_dlls;
    std::vector<std::string> native_lua;
    std::vector<std::string> native_py;
    std::vector<std::string> errors;
};

class ExtensionsPanel final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<ExtensionsPanel> Create();

    struct Hooks {
        // Data feed — the shell scans ExtHost and returns the row
        // snapshot. Reload() pulls through this when set.
        std::function<std::vector<ExtensionRow>()> enumerate;
        // Enable switch per row (session gating — see the header).
        std::function<void(const std::string& id, bool enabled)>
            on_set_enabled;
        // "Run" — re-executes the entry point (ExtHost::Run).
        std::function<void(const std::string& id)> on_run;
        // "UI" — opens the extension's declared xer-ext:// page.
        std::function<void(const std::string& id)> on_open_ui;
        // "Reveal" — OS-shell reveal of the .xexrj path.
        std::function<void(const std::string& path)> on_reveal;
        // "Reload" — the shell rescans then pushes SetExtensions.
        std::function<void()> on_rescan;
        std::function<void(const std::string&, bool)> on_status;
    };
    void SetHooks(Hooks hooks);
    const Hooks& GetHooks() const { return hooks_; }

    // ---- data ----------------------------------------------------
    void SetExtensions(std::vector<ExtensionRow> rows);
    int ExtensionCount() const {
        return static_cast<int>(rows_.size());
    }
    const ExtensionRow* ExtensionAt(int index) const;
    const ExtensionRow* ExtensionById(const std::string& id) const;
    // Pull through the enumerate hook (the Reload button's path).
    void Reload();

    // ---- verbs (row controls + smoke drive these) ----------------
    void ToggleEnabled(const std::string& id);
    bool SetEnabled(const std::string& id, bool enabled);
    bool RunExtension(const std::string& id);
    bool OpenExtensionUi(const std::string& id);
    bool RevealExtension(const std::string& id);
    // Settings gear — builds + shows the manifest details dialog.
    // Returns false for an unknown id (no dialog shown).
    bool OpenSettings(const std::string& id);
    // The last id the settings dialog was opened for (smoke seam —
    // the dialog itself needs a tree to render).
    const std::string& LastSettingsId() const {
        return last_settings_id_;
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {240.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {320.0f, 340.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit ExtensionsPanel();

    // One list row: drawn name/version/status text + the child
    // Switch + gear + Run/UI/Reveal buttons.
    class RowWidget;
    class RowList;
    class InfoBody;   // multiline text for the settings Dialog

    void EnsureChildren();
    void RebuildRows();
    void SyncStatusText();
    void SetStatus(const std::string& text, bool is_error = false);

    std::vector<ExtensionRow> rows_;
    std::string last_settings_id_;

    std::shared_ptr<XER::UI::Widgets::Button> reload_btn_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> scroll_area_;
    std::shared_ptr<RowList> list_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    std::shared_ptr<XER::UI::Widgets::Dialog> settings_dialog_;

    bool children_attached_ = false;
    Hooks hooks_;
};

}  // namespace XER::VideoEditor::Ui2
