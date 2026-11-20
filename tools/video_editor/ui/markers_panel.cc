#include "markers_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
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

    title_ = std::make_shared<Widgets::Label>("MARKERS");
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

    AddChild(title_);
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
    playhead_ = timeline_seconds;
}

void MarkersPanelWidget::RebuildRows() {
    namespace Widgets = XER::UI::Widgets;
    // Sweep the previous generation's retirees — by now any event that
    // was dispatching through them has long returned.
    retired_rows_.clear();
    for (Row& row : rows_) {
        // Disable first so a stale mouse-capture can't fire a detached
        // button's action, then retire it alive one rebuild (see
        // retired_rows_ in the header).
        for (const auto& button : {row.jump, row.remove}) {
            button->SetEnabled(false);
            RemoveChild(button);
            retired_rows_.push_back(button);
        }
    }
    rows_.clear();
    rows_.reserve(markers_.size());
    for (std::size_t i = 0; i < markers_.size(); ++i) {
        Row row;
        const double time = markers_[i].time;
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
        AddChild(row.jump);
        AddChild(row.remove);
        rows_.push_back(std::move(row));
    }
    RequestLayout();
}

Rect MarkersPanelWidget::RowRect(int row) const {
    const auto bounds = GetBounds();
    return {bounds.x + 8.0f,
            rows_top_ + static_cast<float>(row) * row_height_ - scroll_y_,
            bounds.width - 16.0f, row_height_ - 3.0f};
}

Rect MarkersPanelWidget::NameRect(int row) const {
    const Rect r = RowRect(row);
    // After swatch (12px) + time column (~92px), before [Go][x] (~64px).
    return {r.x + 112.0f, r.y + 4.0f,
            std::max(20.0f, r.width - 112.0f - 66.0f), r.height - 8.0f};
}

int MarkersPanelWidget::RowAt(
    const XER::UI::Layout::Point& position) const {
    const auto bounds = GetBounds();
    if (position.y < rows_top_ || position.y >= bounds.y + bounds.height - 34.0f ||
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
    if (markers_.empty() || rows_top_ <= 0.0f) {
        renderer.DrawText("press M on the timeline to add markers",
                          {bounds.x + 12.0f,
                           std::max(rows_top_, bounds.y + 64.0f) + 6.0f,
                           bounds.width - 24.0f, 20.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        return;
    }

    // Time/name/swatch are drawn (not Label children) so row clicks hit
    // the panel — Click events do not bubble up from child widgets.
    const float view_bottom = bounds.y + bounds.height - 34.0f;
    for (std::size_t i = 0; i < markers_.size(); ++i) {
        const Rect row = RowRect(static_cast<int>(i));
        if (row.y + row.height < rows_top_ || row.y > view_bottom) {
            continue;
        }
        const Marker& marker = markers_[i];
        if (static_cast<int>(i) == hovered_) {
            renderer.FillRect(row, tokens.hover, tokens.radius_small);
        }
        // Color swatch.
        renderer.FillRect({row.x + 4.0f, row.y + row.height * 0.5f - 5.0f,
                           10.0f, 10.0f},
                          MarkerColor(marker), 2.0f);
        // Playhead proximity tick.
        if (std::fabs(marker.time - playhead_) < 1e-3) {
            renderer.FillRect({row.x - 4.0f, row.y + 4.0f, 2.0f,
                               row.height - 8.0f},
                              tokens.accent, 1.0f);
        }
        renderer.DrawText(FormatMarkerTime(marker.time),
                          {row.x + 20.0f, row.y + 4.0f, 90.0f,
                           row.height - 8.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        if (static_cast<int>(i) != rename_row_) {
            const Rect name = NameRect(static_cast<int>(i));
            renderer.DrawText(marker.name, name,
                              XER::UI::Rendering::TextStyle{
                                  tokens.foreground, tokens.font_body, 400});
        }
    }
}

void MarkersPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;

    title_->Layout({bounds.x + pad, bounds.y + pad * 0.6f,
                    bounds.width - pad * 2.0f, 24.0f},
                   tokens);
    add_btn_->Layout({bounds.x + pad, bounds.y + 34.0f, 120.0f, 26.0f},
                     tokens);
    rows_top_ = bounds.y + 34.0f + 26.0f + tokens.spacing_small;

    const float view_h =
        bounds.y + bounds.height - 34.0f - rows_top_;
    const float content =
        static_cast<float>(markers_.size()) * row_height_;
    scroll_y_ = std::clamp(scroll_y_, 0.0f,
                           std::max(0.0f, content - std::max(view_h, 0.0f)));

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Rect row = RowRect(static_cast<int>(i));
        rows_[i].jump->Layout({row.Right() - 62.0f, row.y + 2.0f, 34.0f,
                               row.height - 4.0f},
                              tokens);
        rows_[i].remove->Layout({row.Right() - 26.0f, row.y + 2.0f,
                                 24.0f, row.height - 4.0f},
                                tokens);
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
            const float view_h =
                GetBounds().height - (rows_top_ - GetBounds().y) - 34.0f;
            const float range = std::max(
                0.0f, static_cast<float>(markers_.size()) * row_height_ -
                          std::max(view_h, 0.0f));
            if (range <= 0.0f) return false;
            scroll_y_ = std::clamp(
                scroll_y_ - static_cast<float>(event.wheel_delta) * 0.5f,
                0.0f, range);
            Invalidate();
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
