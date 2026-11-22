#include "markers_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/item.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

// HH:MM:SS.mmm (hours grow past 99 naturally via %02d semantics).
std::string FormatMarkerTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const long long millis = static_cast<long long>(seconds * 1000.0 + 0.5);
    const long long hours = millis / 3600000;
    const long long minutes = (millis / 60000) % 60;
    const long long secs = (millis / 1000) % 60;
    const long long ms = millis % 1000;
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld.%03lld",
                  hours, minutes, secs, ms);
    return buffer;
}

Color MarkerColor(const Marker& marker) {
    return {marker.color[0] / 255.0f, marker.color[1] / 255.0f,
            marker.color[2] / 255.0f, 1.0f};
}

// Leading color chip for a marker Item row. Decorative only — created
// DISABLED so hit-testing skips it (disabled widgets never become the
// event target) and row clicks keep landing on the panel. Rendering
// ignores the disabled flag, so the swatch keeps its full color.
class MarkerSwatch final : public XER::UI::Widgets::Widget {
public:
    explicit MarkerSwatch(Color color) : color_(color) {
        SetFocusable(false);
        SetEnabled(false);
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        const float side =
            std::min({10.0f, b.width, b.height});
        const Rect swatch{b.x + (b.width - side) * 0.5f,
                          b.y + (b.height - side) * 0.5f, side, side};
        renderer.FillRect(swatch, color_, tokens.radius_small);
        // Hairline keeps pale marker colors readable on the row fill.
        renderer.StrokeRect(swatch, tokens.border, 1.0f,
                            tokens.radius_small);
    }
    void Layout(const Rect& bounds) override { SetBounds(bounds); }
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    XER::UI::Layout::Size GetMinSize() const override {
        return {10.0f, 10.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {14.0f, 14.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {14.0f, 14.0f};
    }

private:
    Color color_;
};

}  // namespace

MarkersPanelWidget::MarkersPanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // rows scroll inside the panel
}

void MarkersPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    add_btn_ = std::make_shared<Widgets::Button>(
        "Add @ playhead", Widgets::ButtonStyle::Secondary);
    add_btn_->SetTooltip("Add a marker at the current playhead (M)");
    add_btn_->SetOnClick([this] {
        if (hooks_.on_add_marker) {
            hooks_.on_add_marker(playhead_, "Marker");
        }
    });
    rename_input_ = std::make_shared<Widgets::TextInput>("");
    rename_input_->SetVisible(false);
    rename_input_->SetOnSubmit([this] { CommitRename(); });
    status_ = std::make_shared<Widgets::Label>("No markers", true);

    AddChild(add_btn_);
    AddChild(rename_input_);
    AddChild(status_);
    RebuildRows();
}

void MarkersPanelWidget::SetProject(const Project* project) {
    markers_ = project == nullptr ? std::vector<Marker>()
                                  : project->markers;
    CancelRename();
    hovered_ = -1;
    last_click_row_ = -1;
    scroll_y_ = 0.0f;
    if (children_attached_) {
        RebuildRows();
        status_->SetText(markers_.empty()
                             ? "No markers"
                             : std::to_string(markers_.size()) +
                                   " marker(s)");
    }
    Invalidate();
}

void MarkersPanelWidget::SetPlayhead(double timeline_seconds) {
    if (timeline_seconds == playhead_) return;
    playhead_ = timeline_seconds;
    // The current-marker highlight depends on playhead_ (Render matches
    // marker.time within 1e-3), so a moved playhead repaints the panel.
    Invalidate();
}

void MarkersPanelWidget::RebuildRows() {
    namespace Widgets = XER::UI::Widgets;
    // Sweep the previous generation's retirees — by now any event that
    // was dispatching through them has long returned.
    retired_rows_.clear();
    for (Row& row : rows_) {
        // Disable first so a stale mouse-capture can't fire a detached
        // widget's action, then retire it alive one rebuild (see
        // retired_rows_ in the header).
        const std::shared_ptr<Widgets::Widget> children[] = {
            row.swatch, row.item, row.jump, row.remove};
        for (const auto& child : children) {
            child->SetEnabled(false);
            RemoveChild(child);
            retired_rows_.push_back(child);
        }
    }
    rows_.clear();
    rows_.reserve(markers_.size());
    for (std::size_t i = 0; i < markers_.size(); ++i) {
        Row row;
        const Marker& marker = markers_[i];
        const double time = marker.time;
        row.swatch = std::make_shared<MarkerSwatch>(MarkerColor(marker));
        // Item carries name (title) + time (description). Disabled:
        // Click/MouseMove do not bubble, so the row must stay
        // transparent to hit-testing for the panel's click-to-seek
        // and double-click rename to keep working.
        row.item = std::make_shared<Widgets::Item>();
        row.item->SetTitle(marker.name);
        row.item->SetDescription(FormatMarkerTime(time));
        row.item->SetEnabled(false);
        row.jump = std::make_shared<Widgets::Button>(
            "Go", Widgets::ButtonStyle::Ghost);
        row.jump->SetTooltip("Seek to marker");
        row.jump->SetOnClick([this, time] {
            if (hooks_.on_seek) hooks_.on_seek(time);
        });
        row.remove = std::make_shared<Widgets::Button>(
            "x", Widgets::ButtonStyle::Ghost);
        row.remove->SetTooltip("Delete marker");
        row.remove->SetOnClick([this, time] {
            if (hooks_.on_remove_marker) hooks_.on_remove_marker(time);
        });
        AddChild(row.swatch);
        AddChild(row.item);
        AddChild(row.jump);
        AddChild(row.remove);
        rows_.push_back(std::move(row));
    }
    RequestLayout();
}

Rect MarkersPanelWidget::RowRect(int row) const {
    const auto bounds = GetBounds();
    return {bounds.x + VeTheme::kSpaceSm,
            rows_top_ + static_cast<float>(row) * row_height_ - scroll_y_,
            bounds.width - VeTheme::kSpaceSm * 2.0f,
            row_height_ - VeTheme::kSpaceXxs};
}

Rect MarkersPanelWidget::NameRect(int row) const {
    const Rect r = RowRect(row);
    const auto& tokens = CachedLayoutTokens();
    // The Item's title band: after the swatch column, before the
    // [Go][x] button column.
    const float text_x = r.x + 24.0f + tokens.spacing_medium;
    return {text_x, r.y + tokens.spacing_small,
            std::max(40.0f, r.Right() - 70.0f - tokens.spacing_medium -
                                text_x),
            tokens.font_body * 1.4f + 4.0f};
}

float MarkersPanelWidget::RowsViewportBottom() const {
    const auto bounds = GetBounds();
    return bounds.y + bounds.height - 34.0f;
}

int MarkersPanelWidget::RowAt(
    const XER::UI::Layout::Point& position) const {
    const auto bounds = GetBounds();
    if (position.y < rows_top_ || position.y >= RowsViewportBottom() ||
        position.x < bounds.x || position.x >= bounds.x + bounds.width) {
        return -1;
    }
    const int row = static_cast<int>(
        std::floor((position.y - rows_top_ + scroll_y_) / row_height_));
    return row >= 0 && row < static_cast<int>(markers_.size()) ? row : -1;
}

void MarkersPanelWidget::BeginRename(int row) {
    if (row < 0 || row >= static_cast<int>(markers_.size())) return;
    rename_row_ = row;
    // Clear the Item's title while the TextInput covers its band.
    if (row < static_cast<int>(rows_.size())) {
        rows_[static_cast<std::size_t>(row)].item->SetTitle("");
    }
    rename_input_->SetValue(markers_[static_cast<std::size_t>(row)].name);
    rename_input_->SetVisible(true);
    RequestLayout();
    Invalidate();
}

void MarkersPanelWidget::CommitRename() {
    if (rename_row_ < 0 ||
        rename_row_ >= static_cast<int>(markers_.size())) {
        CancelRename();
        return;
    }
    const Marker& marker =
        markers_[static_cast<std::size_t>(rename_row_)];
    const std::string name = rename_input_->GetValue();
    if (hooks_.on_rename_marker && !name.empty() && name != marker.name) {
        hooks_.on_rename_marker(marker.time, name);
        SetStatus("marker renamed");
    }
    CancelRename();
}

void MarkersPanelWidget::CancelRename() {
    if (rename_row_ >= 0) {
        // Restore the title cleared by BeginRename (the row may be
        // gone already if the hook rebuilt the list mid-commit).
        if (children_attached_ &&
            rename_row_ < static_cast<int>(rows_.size()) &&
            rename_row_ < static_cast<int>(markers_.size())) {
            rows_[static_cast<std::size_t>(rename_row_)].item->SetTitle(
                markers_[static_cast<std::size_t>(rename_row_)].name);
        }
        rename_row_ = -1;
        if (children_attached_) rename_input_->SetVisible(false);
        Invalidate();
    }
}

void MarkersPanelWidget::SetStatus(const std::string& text, bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void MarkersPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    // Collapsible-style panel header — the shared VeIcons chrome.
    const float pad = tokens.spacing_medium;
    VeIcons::DrawSectionHeader(
        renderer, tokens,
        {bounds.x + pad, bounds.y + pad * 0.6f,
         bounds.width - pad * 2.0f, VeTheme::kRowHeight},
        "Markers", false);
    if (markers_.empty() || rows_top_ <= 0.0f) {
        renderer.DrawText("press M on the timeline to add markers",
                          {bounds.x + 12.0f,
                           std::max(rows_top_, bounds.y + 64.0f) + 6.0f,
                           bounds.width - 24.0f, 20.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        return;
    }

    // Rows are Item children now — the panel only paints the row
    // chrome behind them: hover wash plus the playhead-proximity tick
    // (the Item's own selected wash stays unused so hover tracking is
    // driven by the panel's single hovered_ index).
    const float view_bottom = RowsViewportBottom();
    for (std::size_t i = 0; i < markers_.size(); ++i) {
        const Rect row = RowRect(static_cast<int>(i));
        if (row.y + row.height < rows_top_ || row.y > view_bottom) {
            continue;
        }
        if (static_cast<int>(i) == hovered_) {
            renderer.FillRect(row, tokens.hover, tokens.radius_small);
        }
        const Marker& marker = markers_[i];
        if (std::fabs(marker.time - playhead_) < 1e-3) {
            renderer.FillRect({row.x - 4.0f, row.y + 4.0f, 2.0f,
                               row.height - 8.0f},
                              tokens.accent, 1.0f);
        }
    }
}

void MarkersPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;

    // The header row is drawn chrome (see Render) — controls keep
    // their old band offsets.
    add_btn_->Layout({bounds.x + pad, bounds.y + 34.0f, 132.0f,
                      VeTheme::kControlHeight},
                     tokens);
    rows_top_ = bounds.y + 34.0f + VeTheme::kControlHeight +
                tokens.spacing_small;

    const float view_bottom = RowsViewportBottom();
    const float view_h = view_bottom - rows_top_;
    const float content =
        static_cast<float>(markers_.size()) * row_height_;
    scroll_y_ = std::clamp(scroll_y_, 0.0f,
                           std::max(0.0f, content - std::max(view_h, 0.0f)));

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Rect row = RowRect(static_cast<int>(i));
        Row& r = rows_[i];
        // Hide children of rows scrolled outside the viewport — the
        // panel clip rect covers the title/status bands too, so
        // without this an off-viewport child would paint (and
        // hit-test) over the chrome.
        const bool visible =
            row.y + row.height > rows_top_ && row.y < view_bottom;
        r.swatch->Layout({row.x + 6.0f,
                          row.y + row.height * 0.5f - 7.0f, 14.0f, 14.0f},
                         tokens);
        r.item->Layout({row.x + 24.0f, row.y,
                        std::max(20.0f, row.width - 24.0f - 70.0f),
                        row.height},
                       tokens);
        r.jump->Layout({row.Right() - 64.0f,
                        row.y + row.height * 0.5f - 13.0f, 36.0f, 26.0f},
                       tokens);
        r.remove->Layout({row.Right() - 26.0f,
                          row.y + row.height * 0.5f - 13.0f, 24.0f,
                          26.0f},
                        tokens);
        r.swatch->SetVisible(visible);
        r.item->SetVisible(visible);
        r.jump->SetVisible(visible);
        r.remove->SetVisible(visible);
    }
    if (rename_row_ >= 0) {
        rename_input_->Layout(NameRect(rename_row_), tokens);
    }
    status_->Layout({bounds.x + pad,
                     bounds.y + bounds.height - 28.0f,
                     bounds.width - pad * 2.0f, 22.0f},
                    tokens);
}

bool MarkersPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    switch (event.type) {
        case XER::UI::Widgets::Event::Type::MouseWheel: {
            const float view_h = RowsViewportBottom() - rows_top_;
            const float range = std::max(
                0.0f, static_cast<float>(markers_.size()) * row_height_ -
                          std::max(view_h, 0.0f));
            if (range <= 0.0f) return false;
            scroll_y_ = std::clamp(
                scroll_y_ - static_cast<float>(event.wheel_delta) * 0.5f,
                0.0f, range);
            Invalidate();
            RequestLayout();   // row children track the scroll offset
            return true;
        }
        case XER::UI::Widgets::Event::Type::MouseMove: {
            const int row = RowAt(event.position);
            if (row != hovered_) {
                hovered_ = row;
                Invalidate();
            }
            return false;
        }
        case XER::UI::Widgets::Event::Type::Click: {
            const int row = RowAt(event.position);
            // Click outside the rows cancels an in-flight rename.
            if (row < 0) {
                if (rename_row_ >= 0) {
                    CancelRename();
                    return true;
                }
                return false;
            }
            const auto now = std::chrono::steady_clock::now();
            const bool double_click =
                row == last_click_row_ &&
                (now - last_click_time_) < kDoubleClickWindow;
            last_click_row_ = row;
            last_click_time_ = now;
            if (double_click) {
                BeginRename(row);
                return true;
            }
            if (rename_row_ >= 0 && rename_row_ != row) CancelRename();
            if (hooks_.on_seek &&
                row < static_cast<int>(markers_.size())) {
                hooks_.on_seek(markers_[static_cast<std::size_t>(row)].time);
            }
            return true;
        }
        default:
            return false;
    }
}

void MarkersPanelWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
