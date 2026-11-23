// ExtensionsPanel — see extensions_panel.h for the real-vs-panel
// seam split. Rows are per-row composite widgets in a ScrollArea
// (small list; each row hosts a Switch + buttons so the draw-only
// VirtualList doesn't fit). The settings gear opens a Dialog with a
// drawn manifest-detail body.
#include "extensions_panel.h"

#include <algorithm>
#include <utility>

#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/dialog.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"
#include "widgets/switch.h"

#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;

constexpr float kToolbarH = 30.0f;
constexpr float kToolbarGap = 6.0f;
constexpr float kStatusH = 18.0f;
constexpr float kRowH = 52.0f;

}  // namespace

// ---- row widget ---------------------------------------------------------

class ExtensionsPanel::RowWidget final : public W::Widget {
public:
    RowWidget(ExtensionsPanel* owner, std::string id)
        : owner_(owner), id_(std::move(id)) {
        SetFocusable(false);
        VeIcons::InstallButtonIconResolver();   // idempotent
        enabled_ = std::make_shared<W::Switch>(true);
        enabled_->SetTooltip("Enable / disable (session)");
        enabled_->SetOnChanged([this](bool on) {
            if (owner_ != nullptr) owner_->SetEnabled(id_, on);
        });
        settings_btn_ = std::make_shared<W::Button>(
            "", W::ButtonStyle::Ghost);
        settings_btn_->SetIcon("nav.settings");
        settings_btn_->SetSize(W::ButtonSize::Small);
        settings_btn_->SetTooltip("Extension details");
        settings_btn_->SetOnClick([this] {
            if (owner_ != nullptr) owner_->OpenSettings(id_);
        });
        run_btn_ = std::make_shared<W::Button>(
            "Run", W::ButtonStyle::Ghost);
        run_btn_->SetIcon("transport.play");
        run_btn_->SetSize(W::ButtonSize::Small);
        run_btn_->SetTooltip("Run the extension's entry point");
        run_btn_->SetOnClick([this] {
            if (owner_ != nullptr) owner_->RunExtension(id_);
        });
        ui_btn_ = std::make_shared<W::Button>(
            "UI", W::ButtonStyle::Ghost);
        ui_btn_->SetIcon("ext.ui");
        ui_btn_->SetSize(W::ButtonSize::Small);
        ui_btn_->SetTooltip("Open the extension's xer-ext:// page");
        ui_btn_->SetOnClick([this] {
            if (owner_ != nullptr) owner_->OpenExtensionUi(id_);
        });
        reveal_btn_ = std::make_shared<W::Button>(
            "", W::ButtonStyle::Ghost);
        reveal_btn_->SetIcon("ext.folder");
        reveal_btn_->SetSize(W::ButtonSize::Small);
        reveal_btn_->SetTooltip("Reveal the .xexrj in Explorer");
        reveal_btn_->SetOnClick([this] {
            if (owner_ != nullptr) owner_->RevealExtension(id_);
        });
        AddChild(enabled_);
        AddChild(settings_btn_);
        AddChild(run_btn_);
        AddChild(ui_btn_);
        AddChild(reveal_btn_);
    }

    const std::string& Id() const { return id_; }

    void Sync(const ExtensionRow& row) {
        enabled_->SetChecked(row.enabled);
        run_btn_->SetEnabled(row.enabled && row.verified);
        ui_btn_->SetVisible(row.has_ui);
        ui_btn_->SetEnabled(row.enabled && row.verified &&
                            row.has_ui);
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        const ExtensionRow* row =
            owner_ != nullptr ? owner_->ExtensionById(id_) : nullptr;
        if (row == nullptr) return;
        // Name + version.
        std::string title = row->name;
        if (!row->version.empty()) title += "  v" + row->version;
        renderer.DrawText(
            title,
            {b.x + Theme::kSpaceSm, b.y + 6.0f,
             b.width - 190.0f, 16.0f},
            {row->enabled ? tokens.foreground : tokens.disabled,
             Theme::kFontBody, 500});
        // Status line: verified · native · UI · error.
        std::string meta = row->status;
        if (row->has_native) meta += " · native";
        if (row->has_ui) meta += " · UI";
        renderer.DrawText(
            meta,
            {b.x + Theme::kSpaceSm, b.y + 24.0f,
             b.width - 190.0f, 14.0f},
            {row->verified ? tokens.muted : tokens.danger,
             Theme::kFontCaption, 400});
    }
    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        const float cy = bounds.y + (bounds.height - 24.0f) * 0.5f;
        // Right-aligned control cluster: reveal ui run | gear switch
        float x = bounds.x + bounds.width - Theme::kSpaceSm;
        const float sw_w = 40.0f;
        enabled_->Layout({x - sw_w, cy, sw_w, 24.0f}, tokens);
        x -= sw_w + 4.0f;
        const float bw = 28.0f;
        settings_btn_->Layout({x - bw, cy, bw, 24.0f}, tokens);
        x -= bw + 4.0f;
        run_btn_->Layout({x - bw - 8.0f, cy, bw + 8.0f, 24.0f},
                         tokens);
        x -= bw + 12.0f;
        ui_btn_->Layout({x - bw, cy, bw, 24.0f}, tokens);
        x -= bw + 4.0f;
        reveal_btn_->Layout({x - bw, cy, bw, 24.0f}, tokens);
    }
    using Widget::Layout;
    bool HandleEvent(const W::Event&) override { return false; }
    Layout::Size GetMinSize() const override {
        return {200.0f, kRowH};
    }
    Layout::Size GetPreferredSize() const override {
        return {300.0f, kRowH};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, kRowH};
    }

private:
    ExtensionsPanel* owner_;
    std::string id_;
    std::shared_ptr<W::Switch> enabled_;
    std::shared_ptr<W::Button> settings_btn_, run_btn_, ui_btn_,
        reveal_btn_;
};

// ---- scroll content ------------------------------------------------------

class ExtensionsPanel::RowList final : public W::Widget {
public:
    void SetRows(std::vector<std::shared_ptr<RowWidget>> rows) {
        // Retire the previous generation (same mark-and-sweep the
        // legacy lists ran — a firing button can never be freed
        // under its own event).
        retired_.clear();
        for (auto& row : rows_) {
            row->SetEnabled(false);
            RemoveChild(row);
            retired_.push_back(std::move(row));
        }
        rows_ = std::move(rows);
        for (const auto& row : rows_) AddChild(row);
        RequestLayout();
        Invalidate();
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        // Hairline separators between rows.
        const auto b = GetBounds();
        for (std::size_t i = 1; i < rows_.size(); ++i) {
            const float y = b.y + static_cast<float>(i) * kRowH;
            renderer.DrawLine({b.x + Theme::kSpaceSm, y},
                              {b.x + b.width - Theme::kSpaceSm, y},
                              tokens.border, 1.0f);
        }
    }
    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        float y = bounds.y;
        for (const auto& row : rows_) {
            row->Layout({bounds.x, y, bounds.width, kRowH}, tokens);
            y += kRowH;
        }
    }
    using Widget::Layout;
    bool HandleEvent(const W::Event&) override { return false; }
    Layout::Size GetMinSize() const override { return {0.0f, 0.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {0.0f, GetContentHeight()};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    float GetContentHeight() const override {
        return kRowH * static_cast<float>(rows_.size());
    }

private:
    std::vector<std::shared_ptr<RowWidget>> rows_;
    std::vector<std::shared_ptr<RowWidget>> retired_;
};

// ---- settings dialog body --------------------------------------------------

// Multi-line manifest detail view — Label only paints a single
// line, so the dialog content is a drawn text block.
class ExtensionsPanel::InfoBody final : public W::Widget {
public:
    explicit InfoBody(std::string text) : text_(std::move(text)) {}
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        // Split on '\n' — plain lines, muted body text.
        float y = b.y + 4.0f;
        std::size_t pos = 0;
        while (pos <= text_.size()) {
            const std::size_t nl = text_.find('\n', pos);
            const std::string_view line =
                nl == std::string::npos
                    ? std::string_view(text_).substr(pos)
                    : std::string_view(text_).substr(pos, nl - pos);
            if (y > b.y + b.height - 14.0f) break;
            const bool header =
                line.size() > 2 && line.back() == ':';
            renderer.DrawText(
                line,
                {b.x, y, b.width, 16.0f},
                {header ? tokens.foreground : tokens.muted,
                 header ? Theme::kFontBody : Theme::kFontCaption,
                 header ? 600 : 400});
            y += header ? 20.0f : 16.0f;
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
    }
    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
    }
    using Widget::Layout;
    bool HandleEvent(const W::Event&) override { return false; }
    Layout::Size GetMinSize() const override {
        return {280.0f, 160.0f};
    }
    Layout::Size GetPreferredSize() const override {
        return {360.0f, 200.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 400.0f};
    }

private:
    std::string text_;
};

// ---- panel ----------------------------------------------------------------

std::shared_ptr<ExtensionsPanel> ExtensionsPanel::Create() {
    return std::shared_ptr<ExtensionsPanel>(new ExtensionsPanel());
}

ExtensionsPanel::ExtensionsPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void ExtensionsPanel::SetHooks(Hooks hooks) {
    hooks_ = std::move(hooks);
}

void ExtensionsPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    reload_btn_ = std::make_shared<W::Button>(
        "Reload", W::ButtonStyle::Secondary);
    reload_btn_->SetSize(W::ButtonSize::Small);
    reload_btn_->SetTooltip("Rescan assets/extensions/");
    reload_btn_->SetOnClick([this] {
        if (hooks_.on_rescan) hooks_.on_rescan();
        else Reload();
    });

    list_ = std::make_shared<RowList>();
    scroll_area_ = std::make_shared<W::ScrollArea>(list_);

    status_ = std::make_shared<W::Label>("No extensions", true);

    AddChild(reload_btn_);
    AddChild(scroll_area_);
    AddChild(status_);
    RebuildRows();
    SyncStatusText();
}

void ExtensionsPanel::SetExtensions(std::vector<ExtensionRow> rows) {
    rows_ = std::move(rows);
    RebuildRows();
    SyncStatusText();
}

const ExtensionRow* ExtensionsPanel::ExtensionAt(int index) const {
    if (index < 0 || index >= static_cast<int>(rows_.size())) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(index)];
}

const ExtensionRow* ExtensionsPanel::ExtensionById(
    const std::string& id) const {
    for (const ExtensionRow& r : rows_) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

void ExtensionsPanel::Reload() {
    if (hooks_.enumerate) {
        SetExtensions(hooks_.enumerate());
    } else {
        SyncStatusText();
    }
}

void ExtensionsPanel::RebuildRows() {
    if (list_ == nullptr) return;
    std::vector<std::shared_ptr<RowWidget>> rows;
    rows.reserve(rows_.size());
    for (const ExtensionRow& r : rows_) {
        auto row = std::make_shared<RowWidget>(this, r.id);
        row->Sync(r);
        rows.push_back(std::move(row));
    }
    list_->SetRows(std::move(rows));
}

// ---- verbs -----------------------------------------------------------------

void ExtensionsPanel::ToggleEnabled(const std::string& id) {
    if (const ExtensionRow* r = ExtensionById(id)) {
        SetEnabled(id, !r->enabled);
    }
}

bool ExtensionsPanel::SetEnabled(const std::string& id,
                                 bool enabled) {
    ExtensionRow* row = nullptr;
    for (ExtensionRow& r : rows_) {
        if (r.id == id) row = &r;
    }
    if (row == nullptr || row->enabled == enabled) return false;
    row->enabled = enabled;
    if (hooks_.on_set_enabled) hooks_.on_set_enabled(id, enabled);
    RebuildRows();
    SyncStatusText();
    return true;
}

bool ExtensionsPanel::RunExtension(const std::string& id) {
    const ExtensionRow* row = ExtensionById(id);
    if (row == nullptr) return false;
    if (!row->enabled) {
        SetStatus(id + " is disabled", false);
        return false;
    }
    if (!row->verified) {
        SetStatus(id + " failed verification", true);
        return false;
    }
    if (hooks_.on_run) hooks_.on_run(id);
    return true;
}

bool ExtensionsPanel::OpenExtensionUi(const std::string& id) {
    const ExtensionRow* row = ExtensionById(id);
    if (row == nullptr || !row->has_ui) return false;
    if (!row->enabled) {
        SetStatus(id + " is disabled", false);
        return false;
    }
    if (hooks_.on_open_ui) hooks_.on_open_ui(id);
    return true;
}

bool ExtensionsPanel::RevealExtension(const std::string& id) {
    const ExtensionRow* row = ExtensionById(id);
    if (row == nullptr || hooks_.on_reveal == nullptr) return false;
    hooks_.on_reveal(row->path);
    return true;
}

bool ExtensionsPanel::OpenSettings(const std::string& id) {
    const ExtensionRow* row = ExtensionById(id);
    if (row == nullptr) return false;
    last_settings_id_ = id;
    // Manifest detail sheet — a real per-extension settings schema
    // doesn't exist yet, so the dialog shows the manifest facts the
    // host verified (same content the legacy panel's tooltip chain
    // carried).
    std::string body = row->name;
    body += row->version.empty() ? "\n" : " v" + row->version + "\n\n";
    body += "id: " + row->id + "\n";
    body += "path: " + row->path + "\n";
    body += "entry: " + row->entry + "\n";
    if (!row->ui_member.empty()) {
        body += "ui: " + row->ui_member + "\n";
    }
    body += std::string("verified: ") +
            (row->verified ? "yes" : "NO") + "\n";
    if (row->has_native) body += "native payload: yes\n";
    const auto list = [&body](const char* title,
                              const std::vector<std::string>& v) {
        if (v.empty()) return;
        body += "\n";
        body += title;
        for (const std::string& s : v) body += "\n  " + s;
    };
    list("requires:", row->depends);
    list("native dlls:", row->native_dlls);
    list("native lua:", row->native_lua);
    list("native py:", row->native_py);
    list("errors:", row->errors);
    body += std::string("\n\nenabled (session): ") +
            (row->enabled ? "yes" : "no");

    settings_dialog_ = W::Dialog::Create(
        "Extension — " + row->name,
        std::make_shared<InfoBody>(std::move(body)), "Close", "");
    settings_dialog_->SetOnClosed([this] { HideOverlay(); });
    ShowOverlay(settings_dialog_);
    return true;
}

void ExtensionsPanel::SyncStatusText() {
    if (status_ == nullptr) return;
    if (rows_.empty()) {
        status_->SetText("No extensions found");
        return;
    }
    int enabled = 0, verified = 0;
    for (const ExtensionRow& r : rows_) {
        enabled += r.enabled ? 1 : 0;
        verified += r.verified ? 1 : 0;
    }
    status_->SetText(std::to_string(rows_.size()) +
                     " extension(s) · " + std::to_string(enabled) +
                     " enabled · " + std::to_string(verified) +
                     " verified");
}

void ExtensionsPanel::SetStatus(const std::string& text,
                                bool is_error) {
    if (status_ != nullptr) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void ExtensionsPanel::Render(XER::UI::Rendering::Renderer& renderer,
                             const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void ExtensionsPanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const float x = bounds.x + Theme::kSpaceSm;
    float y = bounds.y + Theme::kSpaceSm;
    const float w = bounds.width - Theme::kSpaceSm * 2.0f;
    const auto& tokens = CachedLayoutTokens();

    const float rel_w = 70.0f;
    reload_btn_->Layout({x + w - rel_w, y, rel_w, kToolbarH},
                        tokens);
    y += kToolbarH + kToolbarGap;

    const float content_h =
        bounds.y + bounds.height - Theme::kSpaceSm - kStatusH - y;
    scroll_area_->Layout({x, y, w, std::max(0.0f, content_h)},
                         tokens);
    status_->Layout(
        {x, y + std::max(0.0f, content_h) + 2.0f, w, kStatusH},
        tokens);
}

void ExtensionsPanel::Tick(double /*dt_seconds*/) {}

}  // namespace XER::VideoEditor::Ui2
