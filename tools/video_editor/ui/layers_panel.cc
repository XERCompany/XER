#include "layers_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "generated_source.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/item.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;

// HH:MM:SS.mmm — same helper shape as MarkersPanelWidget.
std::string FormatTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const long long millis =
        static_cast<long long>(seconds * 1000.0 + 0.5);
    const long long hours = millis / 3600000;
    const long long minutes = (millis / 60000) % 60;
    const long long secs = (millis / 1000) % 60;
    const long long ms = millis % 1000;
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld.%03lld",
                  hours, minutes, secs, ms);
    return buffer;
}

std::string Basename(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string ClipLabel(const Clip& clip) {
    if (!clip.name.empty()) return clip.name;
    if (!clip.source_path.empty()) return Basename(clip.source_path);
    if (clip.is_adjustment) return "Adjustment Layer";
    switch (clip.source.kind) {
        case SourceKind::ColorMatte:
            return "Color Matte";
        case SourceKind::ColorBars:
            return "Color Bars";
        case SourceKind::Text: {
            const ClipTextStyle style =
                ParseClipTextStyle(clip.source.param);
            const std::size_t nl = style.text.find_first_of("\r\n");
            const std::string text = style.text.substr(0, nl);
            return text.empty() ? "Title" : text;
        }
        default:
            return "Clip";
    }
}

const char* KindTag(const Clip& clip) {
    if (clip.is_adjustment) return "ADJ";
    switch (clip.source.kind) {
        case SourceKind::ColorMatte: return "MATTE";
        case SourceKind::ColorBars: return "BARS";
        case SourceKind::Text: return "TEXT";
        default: return "MEDIA";
    }
}

}  // namespace

LayersPanelWidget::LayersPanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // rows scroll inside the panel
}

void LayersPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    title_ = std::make_shared<Widgets::Label>("LAYERS");
    status_ = std::make_shared<Widgets::Label>("No clips", true);
    AddChild(title_);
    AddChild(status_);
    RebuildRows();
}

void LayersPanelWidget::SetProject(const Project* project) {
    project_ = project;
    RefreshEntries();
}

void LayersPanelWidget::SetActiveTrack(int track_index) {
    if (track_index == active_track_) return;
    active_track_ = track_index;
    RefreshEntries();
}

void LayersPanelWidget::RefreshEntries() {
    entries_.clear();
    track_name_.clear();
    if (project_ != nullptr && active_track_ >= 0 &&
        active_track_ < static_cast<int>(project_->tracks.size())) {
        track_name_ = project_->tracks[active_track_].name;
        for (const Clip& clip : project_->clips) {
            if (clip.track != active_track_) continue;
            LayerEntry entry;
            entry.clip_id = clip.id;
            entry.label = ClipLabel(clip);
            entry.kind = KindTag(clip);
            entry.start = clip.timeline_start;
            entry.duration = clip.duration;
            entries_.push_back(std::move(entry));
        }
        std::sort(entries_.begin(), entries_.end(),
                  [](const LayerEntry& a, const LayerEntry& b) {
                      return a.start < b.start;
                  });
    }
    hovered_ = -1;
    scroll_y_ = 0.0f;
    if (children_attached_) {
        RebuildRows();
        status_->SetText(entries_.empty()
                             ? "No clips on this track"
                             : std::to_string(entries_.size()) +
                                   " clip(s)");
    }
    Invalidate();
}

void LayersPanelWidget::SetSelectedClip(const std::string& clip_id) {
    if (clip_id == selected_clip_id_) return;
    selected_clip_id_ = clip_id;
    Invalidate();
}

void LayersPanelWidget::RebuildRows() {
    namespace Widgets = XER::UI::Widgets;
    retired_rows_.clear();
    for (Row& row : rows_) {
        const std::shared_ptr<Widgets::Widget> children[] = {row.item,
                                                           row.jump};
        for (const auto& child : children) {
            child->SetEnabled(false);
            RemoveChild(child);
            retired_rows_.push_back(child);
        }
    }
    rows_.clear();
    rows_.reserve(entries_.size());
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const LayerEntry& entry = entries_[i];
        Row row;
        row.item = std::make_shared<Widgets::Item>();
        row.item->SetTitle(entry.label);
        char desc[80];
        std::snprintf(desc, sizeof(desc), "%s \u00b7 %s \u00b7 %s",
                      entry.kind.c_str(), FormatTime(entry.start).c_str(),
                      FormatTime(entry.duration).c_str());
        row.item->SetDescription(desc);
        row.item->SetEnabled(false);
        row.jump = std::make_shared<Widgets::Button>(
            "Go", Widgets::ButtonStyle::Ghost);
        row.jump->SetTooltip("Seek to clip start");
        const double start = entry.start;
        row.jump->SetOnClick([this, start] {
            if (hooks_.on_seek) hooks_.on_seek(start);
        });
        AddChild(row.item);
        AddChild(row.jump);
        rows_.push_back(std::move(row));
    }
    RequestLayout();
}

Rect LayersPanelWidget::RowRect(int row) const {
    const auto bounds = GetBounds();
    return {bounds.x + VeTheme::kSpaceSm,
            rows_top_ + static_cast<float>(row) * row_height_ - scroll_y_,
            bounds.width - VeTheme::kSpaceSm * 2.0f,
            row_height_ - VeTheme::kSpaceXxs};
}

float LayersPanelWidget::RowsViewportBottom() const {
    const auto bounds = GetBounds();
    return bounds.y + bounds.height - 34.0f;
}

int LayersPanelWidget::RowAt(
    const XER::UI::Layout::Point& position) const {
    const auto bounds = GetBounds();
    if (position.y < rows_top_ || position.y >= RowsViewportBottom() ||
        position.x < bounds.x || position.x >= bounds.x + bounds.width) {
        return -1;
    }
    const int row = static_cast<int>(
        std::floor((position.y - rows_top_ + scroll_y_) / row_height_));
    return row >= 0 && row < static_cast<int>(entries_.size()) ? row
                                                               : -1;
}

void LayersPanelWidget::SetStatus(const std::string& text,
                                  bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void LayersPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f,
                        tokens.radius_large);
    // Track label under the title — the list's context.
    if (!track_name_.empty()) {
        renderer.DrawText(
            track_name_,
            {bounds.x + 12.0f, bounds.y + 30.0f, bounds.width - 24.0f,
             16.0f},
            XER::UI::Rendering::TextStyle{tokens.muted,
                                          tokens.font_small, 500});
    }
    if (entries_.empty() || rows_top_ <= 0.0f) {
        renderer.DrawText("select a track on the timeline",
                          {bounds.x + 12.0f,
                           std::max(rows_top_, bounds.y + 56.0f) + 6.0f,
                           bounds.width - 24.0f, 20.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        return;
    }
    const float view_bottom = RowsViewportBottom();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Rect row = RowRect(static_cast<int>(i));
        if (row.y + row.height < rows_top_ || row.y > view_bottom) {
            continue;
        }
        const bool selected =
            entries_[i].clip_id == selected_clip_id_;
        if (static_cast<int>(i) == hovered_) {
            renderer.FillRect(row, tokens.hover, tokens.radius_small);
        } else if (selected) {
            renderer.FillRect(
                row,
                VeTheme::WithAlpha(tokens.accent, 0.10f),
                tokens.radius_small);
        }
        if (selected) {
            renderer.FillRect({row.x - 4.0f, row.y + 4.0f, 2.0f,
                               row.height - 8.0f},
                              tokens.accent, 1.0f);
        }
    }
}

void LayersPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;

    title_->Layout({bounds.x + pad, bounds.y + pad * 0.6f,
                    bounds.width - pad * 2.0f, 24.0f},
                   tokens);
    rows_top_ = bounds.y + 52.0f;

    const float view_bottom = RowsViewportBottom();
    const float view_h = view_bottom - rows_top_;
    const float content =
        static_cast<float>(entries_.size()) * row_height_;
    scroll_y_ = std::clamp(
        scroll_y_, 0.0f,
        std::max(0.0f, content - std::max(view_h, 0.0f)));

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Rect row = RowRect(static_cast<int>(i));
        Row& r = rows_[i];
        const bool visible =
            row.y + row.height > rows_top_ && row.y < view_bottom;
        r.item->Layout({row.x + 8.0f, row.y,
                        std::max(20.0f, row.width - 8.0f - 48.0f),
                        row.height},
                       tokens);
        r.jump->Layout({row.Right() - 42.0f,
                        row.y + row.height * 0.5f - 13.0f, 36.0f,
                        26.0f},
                       tokens);
        r.item->SetVisible(visible);
        r.jump->SetVisible(visible);
    }
    status_->Layout({bounds.x + pad,
                     bounds.y + bounds.height - 28.0f,
                     bounds.width - pad * 2.0f, 22.0f},
                    tokens);
}

bool LayersPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    switch (event.type) {
        case XER::UI::Widgets::Event::Type::MouseWheel: {
            const float view_h = RowsViewportBottom() - rows_top_;
            const float range = std::max(
                0.0f,
                static_cast<float>(entries_.size()) * row_height_ -
                    std::max(view_h, 0.0f));
            if (range <= 0.0f) return false;
            scroll_y_ = std::clamp(
                scroll_y_ - static_cast<float>(event.wheel_delta) * 0.5f,
                0.0f, range);
            Invalidate();
            RequestLayout();
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
            if (row < 0) return false;
            if (hooks_.on_select_clip &&
                row < static_cast<int>(entries_.size())) {
                hooks_.on_select_clip(
                    entries_[static_cast<std::size_t>(row)].clip_id);
            }
            return true;
        }
        default:
            return false;
    }
}

void LayersPanelWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
