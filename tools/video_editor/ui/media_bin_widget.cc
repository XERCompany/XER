#include "media_bin_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/table.h"
#include "widgets/video_preview_widget.h"

namespace XER::VideoEditor::Ui {
namespace {

std::string Basename(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string FormatDuration(double seconds) {
    if (seconds <= 0.0 || !std::isfinite(seconds)) return "--:--";
    const int minutes = static_cast<int>(seconds / 60.0);
    const int whole = static_cast<int>(seconds) % 60;
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, whole);
    return buffer;
}

std::string FormatKind(const CatalogEntry& entry) {
    return entry.HasVideo() ? "Video" : "Audio";
}

std::string FormatInfo(const CatalogEntry& entry) {
    char buffer[64];
    if (entry.HasVideo()) {
        std::snprintf(buffer, sizeof(buffer), "%dx%d %.2ffps", entry.width,
                      entry.height, entry.fps);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%s",
                      entry.has_audio ? "audio" : "unknown");
    }
    return buffer;
}

}  // namespace

MediaBinWidget::MediaBinWidget(void* parent_hwnd, Hooks hooks)
    : hooks_(std::move(hooks)), parent_hwnd_(parent_hwnd) {
    SetFocusable(true);
}

void MediaBinWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("MEDIA BIN");
    import_btn_ = std::make_shared<Widgets::Button>(
        "Import", Widgets::ButtonStyle::Primary);
    import_btn_->SetTooltip("Import media into the catalog");
    import_btn_->SetOnClick([this] {
        if (hooks_.on_import) hooks_.on_import();
    });
    add_btn_ = std::make_shared<Widgets::Button>(
        "Add to V1", Widgets::ButtonStyle::Secondary);
    add_btn_->SetTooltip("Insert selected media at the playhead");
    add_btn_->SetOnClick([this] {
        const std::string path = SelectedPath();
        if (!path.empty() && hooks_.on_add_to_timeline) {
            hooks_.on_add_to_timeline(path);
        }
    });
    table_ = Widgets::Table::Create();
    table_->SetColumns({{"Name", 0.0f, false},
                        {"Kind", 62.0f, false},
                        {"Dur", 58.0f, false},
                        {"Info", 92.0f, false}});
    table_->SetRowHeight(28.0f);
    table_->SetOnRowClick([this](int row) {
        selected_row_ = row;
        if (row >= 0 && row < static_cast<int>(entries_.size())) {
            const std::string& path =
                entries_[static_cast<std::size_t>(row)].path;
            if (source_preview_ != nullptr) {
                source_preview_->OpenPath(path);
            }
            if (hooks_.on_select) hooks_.on_select(path);
        }
    });
    source_preview_ =
        std::make_shared<Widgets::VideoPreviewWidget>(parent_hwnd_);
    source_preview_->SetTooltip("Source monitor");
    status_ = std::make_shared<Widgets::Label>("Drop media here or Import", true);

    AddChild(title_);
    AddChild(import_btn_);
    AddChild(add_btn_);
    AddChild(table_);
    AddChild(source_preview_);
    AddChild(status_);
}

void MediaBinWidget::SetEntries(std::vector<CatalogEntry> entries) {
    entries_ = std::move(entries);
    RefreshRows();
}

void MediaBinWidget::SetPreviewActive(bool active) {
    if (source_preview_ != nullptr) {
        source_preview_->SetPreviewActive(active);
    }
}

void MediaBinWidget::RefreshRows() {
    EnsureChildren();
    std::vector<std::vector<std::string>> rows;
    rows.reserve(entries_.size());
    for (const CatalogEntry& entry : entries_) {
        rows.push_back({Basename(entry.path), FormatKind(entry),
                        FormatDuration(entry.duration_seconds),
                        FormatInfo(entry)});
    }
    table_->SetRows(std::move(rows));
    if (selected_row_ >= static_cast<int>(entries_.size())) {
        selected_row_ = -1;
    }
    table_->SetSelectedRow(selected_row_);
    add_btn_->SetEnabled(selected_row_ >= 0);
    status_->SetText(entries_.empty()
                         ? "Import media to begin"
                         : std::to_string(entries_.size()) + " item(s)");
    Invalidate();
}

std::string MediaBinWidget::SelectedPath() const {
    if (selected_row_ < 0 ||
        selected_row_ >= static_cast<int>(entries_.size())) {
        return {};
    }
    return entries_[static_cast<std::size_t>(selected_row_)].path;
}

void MediaBinWidget::SetStatus(const std::string& text, bool is_error) {
    status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void MediaBinWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
}

void MediaBinWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float row_h = 30.0f;

    title_->Layout({bounds.x + pad, bounds.y + pad * 0.6f,
                    bounds.width - pad * 2.0f, 24.0f}, tokens);
    const float button_y = bounds.y + 34.0f;
    const float add_w = 100.0f;
    import_btn_->Layout({bounds.x + pad, button_y, 86.0f, row_h}, tokens);
    add_btn_->Layout({bounds.x + pad + 86.0f + gap, button_y, add_w,
                      row_h}, tokens);
    status_->Layout({bounds.x + pad + 86.0f + add_w + gap * 2.0f,
                     button_y,
                     std::max(40.0f, bounds.width - pad * 2.0f - add_w -
                                      86.0f - gap * 2.0f),
                     row_h}, tokens);
    const float content_top = button_y + row_h + gap;
    const float content_h = bounds.y + bounds.height - pad - content_top;
    const float table_h = std::max(80.0f, content_h * 0.48f);
    table_->Layout({bounds.x + pad, content_top,
                    bounds.width - pad * 2.0f, table_h}, tokens);
    source_preview_->Layout(
        {bounds.x + pad, content_top + table_h + gap,
         bounds.width - pad * 2.0f,
         std::max(120.0f, content_h - table_h - gap)},
        tokens);
}

bool MediaBinWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    (void)event;
    return false;
}

void MediaBinWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
