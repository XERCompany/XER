// extensions_panel.cc — see the header for the list↔view contract.
// Rows are real Widgets::Item children inside a ScrollArea (the
// PluginsPanelWidget pattern): a click selects, a double-click or the
// row's trailing "Open" button flips the panel into View mode where
// the shared CEF surface renders the extension's xer-ext:// page.
#include "extensions_panel.h"

#include <algorithm>
#include <utility>

#include "browser_panel_widget.h"
#include "cef/cef_ext_scheme.h"
#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/item.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"

namespace XER::VideoEditor::Ui {
namespace {

// Two-line row: body-size name over a caption-size version/id/status
// line — same metric the plugins list uses.
constexpr float kRowHeight = VeTheme::kFontBody * 1.4f +
                             VeTheme::kFontCaption * 1.4f +
                             VeTheme::kSpaceXs;
constexpr float kRowIcon = VeTheme::kSpaceLg;

// Leading-slot glyph for a row Item (the plugins-panel RowIcon
// convention): draws the extension pack icon centered in the slot,
// disabled so hit-testing skips it and row clicks land on the Item.
class RowIcon final : public XER::UI::Widgets::Widget {
public:
    RowIcon() {
        SetFocusable(false);
        SetEnabled(false);
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        VeIcons::DrawIcon(renderer, "tab.extensions",
                          static_cast<int>(kRowIcon), tokens.foreground,
                          GetBounds());
    }
    void Layout(const XER::UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
    }
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    XER::UI::Layout::Size GetMinSize() const override {
        return {kRowIcon, kRowIcon};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {kRowIcon, kRowIcon};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {kRowIcon, kRowIcon};
    }
};

}  // namespace

// The ScrollArea's content: Item rows plus the selection/hover chrome
// painted behind them — children render after this widget's own pass,
// so the chrome reads as the rows' background exactly like the plugins
// list. Deliberately not focusable: click focus must climb to the
// panel so arrow-key navigation keeps working after a row click.
class ExtensionsPanelWidget::RowList final
    : public XER::UI::Widgets::Widget {
public:
    RowList() { SetFocusable(false); }

    void SetItems(
        std::vector<std::shared_ptr<XER::UI::Widgets::Item>> items) {
        // Retire the previous generation — disable first so a stale
        // mouse capture can't fire a detached row, then keep them
        // alive one rebuild (the MarkersPanelWidget pattern).
        retired_.clear();
        for (auto& item : items_) {
            item->SetEnabled(false);
            RemoveChild(item);
            retired_.push_back(std::move(item));
        }
        items_ = std::move(items);
        for (const auto& item : items_) AddChild(item);
        RequestLayout();
        Invalidate();
    }

    void SetSelected(int index) {
        if (selected_ == index) return;
        selected_ = index;
        Invalidate();
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const auto row = items_[i]->GetBounds();
            if (static_cast<int>(i) == selected_) {
                renderer.FillRect(row, tokens.selection,
                                  tokens.radius_small);
                renderer.StrokeRect(
                    row, VeTheme::WithAlpha(tokens.accent, 0.5f), 1.0f,
                    tokens.radius_small);
            } else if (items_[i]->IsHovered()) {
                renderer.FillRect(row, tokens.hover,
                                  tokens.radius_small);
            }
        }
    }

    void Layout(const XER::UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        float y = bounds.y;
        for (const auto& item : items_) {
            item->Layout(
                {bounds.x + VeTheme::kSpaceSm,
                 y + VeTheme::kSpaceXxs,
                 std::max(0.0f,
                          bounds.width - VeTheme::kSpaceSm * 2.0f),
                 kRowHeight - VeTheme::kSpaceXs},
                tokens);
            y += kRowHeight;
        }
    }
    using Widget::Layout;

    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {0.0f, 0.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {0.0f, GetContentHeight()};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    float GetContentHeight() const override {
        return kRowHeight * static_cast<float>(items_.size());
    }

private:
    std::vector<std::shared_ptr<XER::UI::Widgets::Item>> items_;
    std::vector<std::shared_ptr<XER::UI::Widgets::Item>> retired_;
    int selected_ = -1;
};

ExtensionsPanelWidget::ExtensionsPanelWidget(void* parent_hwnd,
                                             Hooks hooks)
    : parent_hwnd_(parent_hwnd), hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // the list scrolls inside the panel
}

ExtensionsPanelWidget::~ExtensionsPanelWidget() = default;

void ExtensionsPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    VeIcons::InstallButtonIconResolver();   // idempotent

    title_ = std::make_shared<Widgets::Label>("EXTENSIONS");
    refresh_btn_ = std::make_shared<Widgets::Button>(
        "", Widgets::ButtonStyle::Ghost);
    refresh_btn_->SetIcon("action.refresh");
    refresh_btn_->SetTooltip("Rescan assets/extensions/");
    refresh_btn_->SetOnClick([this] {
        if (hooks_.on_rescan) hooks_.on_rescan();
    });

    list_ = std::make_shared<RowList>();
    scroll_area_ = std::make_shared<Widgets::ScrollArea>(list_);

    run_btn_ = std::make_shared<Widgets::Button>(
        "Run", Widgets::ButtonStyle::Secondary);
    run_btn_->SetTooltip("Re-run the extension's entry point");
    run_btn_->SetOnClick([this] { RunSelected(); });
    reveal_btn_ = std::make_shared<Widgets::Button>(
        "Reveal", Widgets::ButtonStyle::Secondary);
    reveal_btn_->SetIcon("ext.folder");
    reveal_btn_->SetTooltip("Reveal the .xexrj in Explorer");
    reveal_btn_->SetOnClick([this] { RevealSelected(); });
    status_ = std::make_shared<Widgets::Label>("", /*muted=*/true);

    back_btn_ = std::make_shared<Widgets::Button>(
        "< Extensions", Widgets::ButtonStyle::Ghost);
    back_btn_->SetTooltip("Back to the extension list");
    back_btn_->SetOnClick([this] { ShowList(); });
    view_label_ =
        std::make_shared<Widgets::Label>("", /*muted=*/true);

    // The shared CEF surface — created up front so the extension
    // host's openUi() funnel can navigate before the first layout;
    // its Chromium child stays hidden until View mode is entered.
    browser_ = std::make_shared<BrowserPanelWidget>(parent_hwnd_);

    AddChild(title_);
    AddChild(refresh_btn_);
    AddChild(scroll_area_);
    AddChild(run_btn_);
    AddChild(reveal_btn_);
    AddChild(status_);
    AddChild(back_btn_);
    AddChild(view_label_);
    AddChild(browser_);

    SyncModeVisibility();
    RebuildList();
    run_btn_->SetEnabled(
        selected_ >= 0 &&
        rows_[static_cast<std::size_t>(selected_)].verified);
    reveal_btn_->SetEnabled(selected_ >= 0);
    status_->SetText(rows_.empty()
                         ? "No extensions found"
                         : std::to_string(rows_.size()) +
                               " extension(s)");
}

void ExtensionsPanelWidget::Reload() {
    const std::string keep =
        selected_ >= 0 && selected_ < static_cast<int>(rows_.size())
            ? rows_[static_cast<std::size_t>(selected_)].id
            : std::string();
    rows_.clear();
    for (const Extension& e : ExtHost::Shared().Entries()) {
        Row row;
        row.id = e.manifest.id;
        row.name = e.manifest.name.empty() ? e.manifest.id
                                          : e.manifest.name;
        row.version = e.manifest.version;
        row.path = e.path;
        row.verified = e.verified;
        row.has_ui = e.verified && !e.manifest.ui.empty();
        row.status = e.verified
                         ? "verified"
                         : (e.verify_errors.empty()
                                ? "verification failed"
                                : e.verify_errors.front());
        rows_.push_back(std::move(row));
    }
    selected_ = -1;
    if (!keep.empty()) {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].id == keep) {
                selected_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (children_attached_) {
        RebuildList();
        run_btn_->SetEnabled(selected_ >= 0);
        reveal_btn_->SetEnabled(selected_ >= 0);
        status_->SetText(rows_.empty()
                             ? "No extensions found"
                             : std::to_string(rows_.size()) +
                                   " extension(s)");
    }
    Invalidate();
}

void ExtensionsPanelWidget::RebuildList() {
    namespace Widgets = XER::UI::Widgets;
    std::vector<std::shared_ptr<Widgets::Item>> items;
    items.reserve(rows_.size());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Row& row = rows_[i];
        auto item = std::make_shared<Widgets::Item>();
        item->SetTitle(row.name);
        std::string desc;
        if (!row.version.empty()) desc += "v" + row.version + " · ";
        desc += row.id + " · " + row.status;
        item->SetDescription(desc);
        item->SetTooltip(row.path.empty() ? row.name : row.path);
        item->AddChild(std::make_shared<RowIcon>());
        if (row.has_ui) {
            // Trailing slot: direct "Open" for extensions that ship a
            // UI — the footer Run/Reveal pair still acts on the row
            // selection for everything else.
            auto open = std::make_shared<Widgets::Button>(
                "Open", Widgets::ButtonStyle::Secondary);
            open->SetIcon("ext.ui");
            open->SetTooltip("Open the extension UI");
            const std::string id = row.id;
            open->SetOnClick([this, id] { OpenExtension(id); });
            item->AddChild(std::move(open));
        }
        const int index = static_cast<int>(i);
        const std::string id = row.id;
        const bool has_ui = row.has_ui;
        item->SetOnClick([this, index] { SelectRow(index); });
        item->SetOnDoubleClick([this, id, has_ui] {
            if (has_ui) {
                OpenExtension(id);
            } else {
                RunSelected();
            }
        });
        items.push_back(std::move(item));
    }
    list_->SetItems(std::move(items));
    list_->SetSelected(selected_);
}

void ExtensionsPanelWidget::SelectRow(int index) {
    if (selected_ == index) return;
    selected_ = index;
    if (children_attached_) {
        list_->SetSelected(selected_);
        run_btn_->SetEnabled(
            selected_ >= 0 &&
            rows_[static_cast<std::size_t>(selected_)].verified);
        reveal_btn_->SetEnabled(selected_ >= 0);
    }
    Invalidate();
}

const ExtensionsPanelWidget::Row* ExtensionsPanelWidget::Selected()
    const {
    if (selected_ < 0 ||
        selected_ >= static_cast<int>(rows_.size())) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(selected_)];
}

void ExtensionsPanelWidget::EnsureSelectionVisible() {
    if (scroll_area_ == nullptr || selected_ < 0) return;
    const float view_h = scroll_area_->GetBounds().height;
    const float top = kRowHeight * static_cast<float>(selected_);
    const float bottom = top + kRowHeight;
    const float scroll = scroll_area_->GetScrollY();
    if (top < scroll) {
        scroll_area_->ScrollTo(top);
    } else if (bottom > scroll + view_h) {
        scroll_area_->ScrollTo(bottom - view_h);
    }
}

void ExtensionsPanelWidget::SetMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    SyncModeVisibility();
    RequestLayout();
    Invalidate();
}

void ExtensionsPanelWidget::SyncModeVisibility() {
    if (!children_attached_) return;
    const bool vis = IsVisible();
    const bool list = vis && mode_ == Mode::List;
    const bool view = vis && mode_ == Mode::View;
    title_->SetVisible(list);
    refresh_btn_->SetVisible(list);
    scroll_area_->SetVisible(list);
    run_btn_->SetVisible(list);
    reveal_btn_->SetVisible(list);
    status_->SetVisible(list);
    back_btn_->SetVisible(view);
    view_label_->SetVisible(view);
    // BrowserPanelWidget::SetVisible also shows/hides the native
    // Chromium HWND — the shadow must run through the derived pointer.
    browser_->SetVisible(view);
}

void ExtensionsPanelWidget::SetVisible(bool visible) {
    XER::UI::Widgets::Widget::SetVisible(visible);
    SyncModeVisibility();
}

void ExtensionsPanelWidget::OpenExtension(const std::string& id) {
    const Row* row = nullptr;
    for (const Row& r : rows_) {
        if (r.id == id) row = &r;
    }
    if (row == nullptr) {
        SetStatus("unknown extension: " + id, true);
        return;
    }
    if (!row->verified) {
        SetStatus(id + ": " + row->status, true);
        return;
    }
    // Look the manifest up for the declared ui member — the snapshot
    // row only keeps has_ui.
    std::string ui;
    for (const Extension& e : ExtHost::Shared().Entries()) {
        if (e.manifest.id == id) ui = e.manifest.ui;
    }
    if (ui.empty()) {
        SetStatus(id + " declares no UI", true);
        return;
    }
    view_title_ = row->name;
    SetMode(Mode::View);
    EnsureChildren();
    view_label_->SetText(view_title_);
    browser_->OpenUrl(XER::Cef::ExtSchemeUrl(id, ui));
    SetStatus("opened " + id);
}

void ExtensionsPanelWidget::OpenUrl(const std::string& url) {
    // xer-ext://<id>/<member> — pick the extension name up for the
    // View-mode header when the id resolves.
    view_title_.clear();
    const std::string prefix = "xer-ext://";
    if (url.rfind(prefix, 0) == 0) {
        const auto end = url.find('/', prefix.size());
        const std::string id = url.substr(
            prefix.size(),
            end == std::string::npos ? end : end - prefix.size());
        for (const Row& r : rows_) {
            if (r.id == id) view_title_ = r.name;
        }
    }
    if (view_title_.empty()) view_title_ = "Extension UI";
    SetMode(Mode::View);
    EnsureChildren();
    if (view_label_) view_label_->SetText(view_title_);
    browser_->OpenUrl(url);
}

void ExtensionsPanelWidget::ShowList() {
    SetMode(Mode::List);
}

void ExtensionsPanelWidget::RunSelected() {
    const Row* row = Selected();
    if (row == nullptr) {
        SetStatus("Select an extension");
        return;
    }
    if (!row->verified) {
        SetStatus(row->id + ": " + row->status, true);
        return;
    }
    const ExtHost::RunResult r = ExtHost::Shared().Run(row->id);
    if (r.ok) {
        SetStatus("ran " + row->id);
    } else {
        SetStatus(row->id + ": " + r.error, true);
    }
}

void ExtensionsPanelWidget::RevealSelected() {
    const Row* row = Selected();
    if (row == nullptr || row->path.empty()) {
        SetStatus("Select an extension");
        return;
    }
    if (hooks_.on_reveal) {
        hooks_.on_reveal(row->path);
    }
}

void ExtensionsPanelWidget::SetStatus(const std::string& text,
                                      bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void ExtensionsPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f,
                        tokens.radius_large);
    if (mode_ == Mode::List && rows_.empty()) {
        const auto list_bounds =
            scroll_area_ != nullptr ? scroll_area_->GetBounds()
                                    : XER::UI::Layout::Rect{};
        const float hint_y =
            list_bounds.height > 0.0f
                ? list_bounds.y + VeTheme::kSpaceXs
                : bounds.y + VeTheme::kSpaceMd * 3.0f + VeTheme::kSpaceXs;
        renderer.DrawText(
            "No extensions found in assets/extensions/",
            {bounds.x + VeTheme::kSpaceMd, hint_y,
             bounds.width - VeTheme::kSpaceMd * 2.0f,
             tokens.font_body * 1.4f},
            XER::UI::Rendering::TextStyle{tokens.muted,
                                          tokens.font_small, 400});
    }
}

void ExtensionsPanelWidget::Layout(
    const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float inner_x = bounds.x + pad;
    const float inner_w = bounds.width - pad * 2.0f;

    if (mode_ == Mode::View) {
        // Back row (same 30px band as the list header) over the CEF
        // surface — the browser widget paints its own card inside.
        const float y = bounds.y + pad * 0.6f;
        back_btn_->Layout({inner_x, y, 108.0f,
                           VeTheme::kPanelHeaderHeight - 4.0f},
                          tokens);
        view_label_->Layout(
            {inner_x + 108.0f + gap, y,
             std::max(0.0f, inner_w - 108.0f - gap),
             VeTheme::kPanelHeaderHeight},
            tokens);
        const float browser_y =
            y + VeTheme::kPanelHeaderHeight + gap;
        browser_->Layout(
            {inner_x, browser_y, inner_w,
             std::max(0.0f, bounds.y + bounds.height - pad * 0.6f -
                                browser_y)},
            tokens);
        return;
    }

    float y = bounds.y + pad * 0.6f;
    // Header row: section title on the left, refresh icon-button on
    // the right — the same 30px panel-header band the other dock
    // panels draw.
    title_->Layout({inner_x, y, inner_w - 34.0f,
                    VeTheme::kPanelHeaderHeight},
                   tokens);
    refresh_btn_->Layout(
        {inner_x + inner_w - 32.0f, y + 1.0f, 32.0f,
         VeTheme::kPanelHeaderHeight - 2.0f},
        tokens);
    y += VeTheme::kPanelHeaderHeight + gap;

    const float footer_h = VeTheme::kControlHeight;
    const float footer_y =
        bounds.y + bounds.height - footer_h - VeTheme::kSpaceXs;
    scroll_area_->Layout(
        {inner_x, y, inner_w, std::max(0.0f, footer_y - gap - y)},
        tokens);
    // Footer action pair on the left, status line filling the rest —
    // matches the plugins panel's footer contract. "Reveal" carries
    // the ext.folder icon, so it gets a wider slot than "Run".
    const float run_w = 60.0f;
    const float reveal_w = 84.0f;
    run_btn_->Layout({inner_x, footer_y, run_w, footer_h}, tokens);
    reveal_btn_->Layout({inner_x + run_w + gap, footer_y, reveal_w,
                         footer_h},
                        tokens);
    status_->Layout(
        {inner_x + run_w + reveal_w + gap * 2.0f,
         footer_y + VeTheme::kSpaceXxs,
         std::max(40.0f, inner_w - run_w - reveal_w - gap * 2.0f),
         footer_h - VeTheme::kSpaceXs},
        tokens);
}

bool ExtensionsPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    if (event.type != XER::UI::Widgets::Event::Type::KeyDown) {
        return false;
    }
    if (mode_ == Mode::View &&
        event.key == XER::UI::Widgets::KeyCode::Escape) {
        ShowList();
        return true;
    }
    if (mode_ != Mode::List) return false;
    if (event.key == XER::UI::Widgets::KeyCode::Enter) {
        if (const Row* row = Selected(); row != nullptr) {
            if (row->has_ui) {
                OpenExtension(row->id);
            } else {
                RunSelected();
            }
        }
        return true;
    }
    if (event.key == XER::UI::Widgets::KeyCode::ArrowDown ||
        event.key == XER::UI::Widgets::KeyCode::ArrowUp) {
        if (!rows_.empty()) {
            const int count = static_cast<int>(rows_.size());
            const int dir =
                event.key == XER::UI::Widgets::KeyCode::ArrowDown ? 1
                                                                : -1;
            const int next = selected_ < 0
                                 ? (dir > 0 ? 0 : count - 1)
                                 : (selected_ + dir + count) % count;
            SelectRow(next);
            EnsureSelectionVisible();
        }
        return true;
    }
    return false;
}

}  // namespace XER::VideoEditor::Ui
