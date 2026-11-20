#include "media_bin_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "app_runtime/ui_manifest.h"
#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "io/oiio_image.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/context_menu.h"
#include "widgets/label.h"
#include "widgets/table.h"

// video_engine.h pulls windows.h, which re-defines DrawText as
// DrawTextA/W after renderer.h already undid it.
#ifdef DrawText
#undef DrawText
#endif

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

// Lowercased extension of a UTF-8 media path, no leading dot —
// OiioHandlesExtension's documented input ("exr", "tiff", ...).
std::string MediaExtensionLower(const std::string& utf8_path) {
    std::string ext = XER::Helper::String::ToLowerAscii(
        XER::Helper::Path::FromUtf8(utf8_path).extension().string());
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    return ext;
}

// OIIO decode results memoized by UTF-8 path — DrawGrid repaints on
// every hover/scroll, and decoding EXR/RAW per paint would thrash
// disk + CPU. Failures are cached too (a corrupt still must not retry
// every frame). Bounded: the whole map clears on overflow — entries
// are shared_ptrs so a clear can never dangle a reference a caller
// still holds.
constexpr std::size_t kOiioThumbCacheMax = 64;

std::shared_ptr<const Io::Image> OiioThumb(const std::string& utf8_path) {
    static std::unordered_map<std::string, std::shared_ptr<Io::Image>>
        cache;
    const auto it = cache.find(utf8_path);
    if (it != cache.end()) {
        return it->second;
    }
    if (cache.size() >= kOiioThumbCacheMax) {
        cache.clear();
    }
    auto image = std::make_shared<Io::Image>(
        Io::ReadImage(XER::Helper::Path::FromUtf8(utf8_path)));
    cache.emplace(utf8_path, image);
    return image;
}

}  // namespace

MediaBinWidget::MediaBinWidget(void* /*parent_hwnd*/, Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
}

void MediaBinWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    // assets/ui/strings.ini — media_bin_title overrides the header.
    title_ = std::make_shared<Widgets::Label>(
        AppRuntime::SharedUiStrings().Get("media_bin_title",
                                          "MEDIA BIN"));
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
        if (row >= 0 && row < static_cast<int>(visible_.size())) {
            selected_row_ = visible_[static_cast<std::size_t>(row)];
        } else {
            selected_row_ = -1;
        }
        if (selected_row_ >= 0) {
            const std::string& path =
                entries_[static_cast<std::size_t>(selected_row_)].path;
            if (hooks_.on_select) hooks_.on_select(path);
            MaybeOpenSource(selected_row_);
        }
    });
    view_btn_ = std::make_shared<Widgets::Button>(
        "Grid", Widgets::ButtonStyle::Ghost);
    view_btn_->SetTooltip("Toggle grid / list view");
    view_btn_->SetOnClick([this] {
        grid_mode_ = !grid_mode_;
        view_btn_->SetText(grid_mode_ ? "Grid" : "List");
        Invalidate();
    });
    status_ = std::make_shared<Widgets::Label>("Import via button or File menu", true);

    AddChild(title_);
    AddChild(import_btn_);
    AddChild(add_btn_);
    AddChild(view_btn_);
    AddChild(table_);
    AddChild(status_);
}

void MediaBinWidget::SetEntries(std::vector<CatalogEntry> entries) {
    entries_ = std::move(entries);
    RefreshRows();
}

void MediaBinWidget::SetPreviewActive(bool /*active*/) {
    // The bin no longer embeds a source monitor — kept as a no-op so
    // callers don't need a feature check.
}

void MediaBinWidget::RebuildVisible() {
    visible_.clear();
    visible_.reserve(entries_.size());
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const CatalogEntry& entry = entries_[static_cast<std::size_t>(i)];
        const bool keep = filter_ == 0 ||
            (filter_ == 1 && entry.HasVideo()) ||
            (filter_ == 2 && !entry.HasVideo() && entry.has_audio);
        if (keep) visible_.push_back(i);
    }
    if (selected_row_ >= 0 &&
        std::find(visible_.begin(), visible_.end(), selected_row_) ==
            visible_.end()) {
        selected_row_ = -1;
    }
}

void MediaBinWidget::RefreshRows() {
    EnsureChildren();
    RebuildVisible();
    std::vector<std::vector<std::string>> rows;
    rows.reserve(visible_.size());
    for (const int index : visible_) {
        const CatalogEntry& entry =
            entries_[static_cast<std::size_t>(index)];
        rows.push_back({Basename(entry.path), FormatKind(entry),
                        FormatDuration(entry.duration_seconds),
                        FormatInfo(entry)});
    }
    table_->SetRows(std::move(rows));
    // Table selection is a *row* index — translate the entry index.
    int sel_row = -1;
    for (int i = 0; i < static_cast<int>(visible_.size()); ++i) {
        if (visible_[static_cast<std::size_t>(i)] == selected_row_) {
            sel_row = i;
            break;
        }
    }
    table_->SetSelectedRow(sel_row);
    add_btn_->SetEnabled(selected_row_ >= 0);
    status_->SetText(entries_.empty()
                         ? "Import media to begin"
                         : std::to_string(entries_.size()) + " item(s)");
    Invalidate();
}

void MediaBinWidget::MaybeOpenSource(int entry_index) {
    // There is no DoubleClick event type — a second Click on the same
    // entry inside the threshold is the open-in-source-monitor
    // gesture (Premiere project panel behavior).
    constexpr auto kDoubleClickWindow = std::chrono::milliseconds(500);
    if (entry_index < 0 ||
        entry_index >= static_cast<int>(entries_.size())) {
        last_click_entry_ = -1;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool is_double =
        entry_index == last_click_entry_ &&
        now - last_click_at_ < kDoubleClickWindow;
    last_click_entry_ = entry_index;
    last_click_at_ = now;
    if (is_double && hooks_.on_open_source) {
        last_click_entry_ = -1;   // a third click is a new gesture
        hooks_.on_open_source(
            entries_[static_cast<std::size_t>(entry_index)].path);
    }
}

std::string MediaBinWidget::SelectedPath() const {
    if (selected_row_ < 0 ||
        selected_row_ >= static_cast<int>(entries_.size())) {
        return {};
    }
    return entries_[static_cast<std::size_t>(selected_row_)].path;
}

void MediaBinWidget::SetOnRemoveEntry(
    std::function<void(const std::string&)> cb) {
    on_remove_entry_ = std::move(cb);
}

void MediaBinWidget::SetOnRevealEntry(
    std::function<void(const std::string&)> cb) {
    on_reveal_entry_ = std::move(cb);
}

void MediaBinWidget::SetStatus(const std::string& text, bool is_error) {
    status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void MediaBinWidget::DrawGrid(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    const float gap = 8.0f;
    const int cols = std::max(
        1, static_cast<int>((grid_rect_.width + gap) / 132.0f));
    const float card_w =
        (grid_rect_.width - gap * (cols - 1)) / cols;
    const float thumb_h = card_w * 0.5625f;  // 16:9 thumbs
    const float card_h = thumb_h + 22.0f;

    // Clamp scroll to content height.
    const int rows =
        (static_cast<int>(visible_.size()) + cols - 1) / cols;
    const float content_h = rows * (card_h + gap);
    const float max_scroll =
        std::max(0.0f, content_h - grid_rect_.height);
    grid_scroll_ = std::clamp(grid_scroll_, 0.0f, max_scroll);

    renderer.PushClip(grid_rect_);
    for (std::size_t i = 0; i < visible_.size(); ++i) {
        const CatalogEntry& entry =
            entries_[static_cast<std::size_t>(visible_[i])];
        const int row = static_cast<int>(i) / cols;
        const int col = static_cast<int>(i) % cols;
        const float cx = grid_rect_.x + col * (card_w + gap);
        const float cy =
            grid_rect_.y + row * (card_h + gap) - grid_scroll_;
        if (cy + card_h < grid_rect_.y ||
            cy > grid_rect_.y + grid_rect_.height) {
            continue;  // outside the viewport
        }
        const XER::UI::Layout::Rect card{cx, cy, card_w, card_h};
        const XER::UI::Layout::Rect thumb{cx, cy, card_w, thumb_h};
        const bool selected = visible_[i] == selected_row_;
        const bool hovered = visible_[i] == grid_hovered_;

        // Poster or a dark placeholder with an audio-bars icon.
        bool drew_poster = false;
        if (entry.HasVideo() && !entry.poster_path.empty()) {
            drew_poster = renderer.DrawImage(
                entry.poster_path, thumb, 1.0f,
                tokens.radius_medium);
        }
        // Stills WIC can't decode (EXR/DPX/TIFF/RAW/PSD/HDR): OIIO
        // reads the source file straight to BGRA — DrawFrameBgra-ready.
        if (!drew_poster &&
            Io::OiioHandlesExtension(MediaExtensionLower(entry.path))) {
            const std::shared_ptr<const Io::Image> img =
                OiioThumb(entry.path);
            if (img != nullptr && img->ok && !img->bgra.empty() &&
                img->width > 0 && img->height > 0) {
                drew_poster = renderer.DrawFrameBgra(
                    img->bgra.data(), img->width, img->height, thumb,
                    1.0f);
            }
        }
        if (!drew_poster) {
            renderer.FillRect(thumb, tokens.panel,
                              tokens.radius_medium);
            const float bars[] = {3.0f, 7.0f, 11.0f, 7.0f, 3.0f};
            const float bcx = thumb.x + thumb.width * 0.5f;
            const float bcy = thumb.y + thumb.height * 0.5f;
            float bx = bcx - 8.0f;
            for (const float b : bars) {
                renderer.FillRect(
                    {bx, bcy - b * 0.5f, 2.0f, b}, tokens.muted, 1.0f);
                bx += 4.0f;
            }
        }
        if (hovered && !selected) {
            renderer.FillRect(
                thumb, {tokens.hover.r, tokens.hover.g, tokens.hover.b,
                        0.25f},
                tokens.radius_medium);
        }
        if (selected) {
            renderer.FillRect(thumb,
                              {tokens.accent.r, tokens.accent.g,
                               tokens.accent.b, 0.12f},
                              tokens.radius_medium);
            renderer.StrokeRect(thumb, tokens.accent, 1.5f,
                                tokens.radius_medium);
        } else {
            renderer.StrokeRect(thumb, tokens.border, 1.0f,
                                tokens.radius_medium);
        }

        // Duration badge, bottom-right of the thumb.
        const std::string dur =
            FormatDuration(entry.duration_seconds);
        const XER::UI::Rendering::TextStyle badge_style{
            tokens.foreground, tokens.font_small, 500};
        const auto bs = renderer.MeasureText(dur, badge_style);
        const XER::UI::Layout::Rect badge{
            thumb.x + thumb.width - bs.width - 8.0f,
            thumb.y + thumb.height - bs.height - 5.0f,
            bs.width + 6.0f, bs.height + 3.0f};
        renderer.FillRect(badge, {0.0f, 0.0f, 0.0f, 0.65f},
                          tokens.radius_small);
        renderer.DrawText(dur,
                          {badge.x + 3.0f, badge.y + 1.5f,
                           badge.width - 6.0f, badge.height - 3.0f},
                          badge_style);

        // Name under the thumb — a small inset keeps text off the
        // card edges; the renderer's clip truncates the tail.
        renderer.DrawText(
            Basename(entry.path),
            {card.x + 4.0f, card.y + thumb_h + 5.0f, card.width - 8.0f,
             16.0f},
            {selected ? tokens.accent : tokens.foreground,
             tokens.font_small, 400});
    }
    renderer.PopClip();

    if (visible_.empty()) {
        renderer.DrawText(entries_.empty()
                              ? "No media — Import to begin"
                              : "No items match the filter",
                          grid_rect_,
                          {tokens.muted, tokens.font_small, 400});
    }
}

void MediaBinWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    DrawChips(renderer, tokens);
    if (grid_mode_) DrawGrid(renderer, tokens);
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
    const float view_w = 44.0f;
    import_btn_->Layout({bounds.x + pad, button_y, 78.0f, row_h}, tokens);
    add_btn_->Layout({bounds.x + pad + 78.0f + gap, button_y, add_w,
                      row_h}, tokens);
    view_btn_->Layout(
        {bounds.x + bounds.width - pad - view_w, button_y, view_w,
         row_h},
        tokens);
    const float status_x =
        bounds.x + pad + 78.0f + add_w + gap * 2.0f;
    const float status_w =
        bounds.x + bounds.width - pad - view_w - gap - status_x;
    // Hide rather than overlap the panel edge on narrow bins.
    status_->SetVisible(status_w >= 36.0f);
    if (status_w > 0.0f) {
        status_->Layout({status_x, button_y, status_w, row_h}, tokens);
    }
    const float content_top = button_y + row_h + gap;
    // Filter chips row sits between the buttons and the content.
    const float chip_h = 24.0f;
    const float list_top = content_top + chip_h + gap;
    const float content_h =
        std::max(0.0f, bounds.y + bounds.height - pad - list_top);
    // The content area belongs to the list/grid entirely — the bin
    // preview monitor was removed, so no split is needed.
    const float table_h = std::max(80.0f, content_h);
    table_->SetVisible(!grid_mode_);
    if (!grid_mode_) {
        table_->Layout({bounds.x + pad, list_top,
                        bounds.width - pad * 2.0f, table_h}, tokens);
    }
    grid_rect_ = {bounds.x + pad, list_top,
                  bounds.width - pad * 2.0f, table_h};
}

XER::UI::Layout::Rect MediaBinWidget::GridRect() const {
    return grid_rect_;
}

namespace {
constexpr const char* kChipLabels[] = {"All", "Video", "Audio"};
}

XER::UI::Layout::Rect MediaBinWidget::ChipRect(int index) const {
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = 6.0f;
    const float widths[] = {44.0f, 52.0f, 52.0f};
    const auto bounds = GetBounds();
    float x = bounds.x + pad;
    const float y = bounds.y + 34.0f + 30.0f + tokens.spacing_small;
    for (int i = 0; i < index; ++i) x += widths[i] + gap;
    return {x, y, widths[index], 24.0f};
}

void MediaBinWidget::DrawChips(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    for (int i = 0; i < 3; ++i) {
        const auto rect = ChipRect(i);
        const bool active = filter_ == i;
        const bool hovered = chip_hovered_ == i;
        if (active || hovered) {
            renderer.FillRect(
                rect,
                active
                    ? XER::UI::Theme::Color{tokens.accent.r,
                                            tokens.accent.g,
                                            tokens.accent.b, 0.22f}
                    : XER::UI::Theme::Color{tokens.hover.r,
                                            tokens.hover.g,
                                            tokens.hover.b, 0.6f},
                rect.height * 0.5f);   // pill
        } else {
            renderer.StrokeRect(rect, tokens.border, 1.0f,
                                rect.height * 0.5f);
        }
        renderer.DrawText(kChipLabels[i],
                          {rect.x, rect.y + 1.0f, rect.width,
                           rect.height},
                          {active ? tokens.accent : tokens.muted,
                           tokens.font_small, active ? 600 : 400});
    }
}

int MediaBinWidget::ItemAtPoint(
    const XER::UI::Layout::Point& p) const {
    if (!grid_rect_.Contains(p)) return -1;
    const float gap = 8.0f;
    const int cols = std::max(
        1, static_cast<int>((grid_rect_.width + gap) / 132.0f));
    const float card_w =
        (grid_rect_.width - gap * (cols - 1)) / cols;
    const float thumb_h = card_w * 0.5625f;  // 16:9
    const float card_h = thumb_h + 22.0f;
    const float rel_y = p.y - grid_rect_.y + grid_scroll_;
    const int row = static_cast<int>(rel_y / (card_h + gap));
    if (row < 0) return -1;
    const float row_y = row * (card_h + gap);
    if (rel_y - row_y > card_h) return -1;  // in the gap
    const int col =
        static_cast<int>((p.x - grid_rect_.x) / (card_w + gap));
    if (col < 0 || col >= cols) return -1;
    const float col_x = col * (card_w + gap);
    if (p.x - grid_rect_.x - col_x > card_w) return -1;  // in the gap
    const int index = row * cols + col;
    return index < static_cast<int>(visible_.size())
        ? visible_[static_cast<std::size_t>(index)]
        : -1;
}

bool MediaBinWidget::ShowContextMenu(
    const XER::UI::Layout::Point p) {
    namespace Widgets = XER::UI::Widgets;
    if (!GetBounds().Contains(p)) return false;
    EnsureChildren();

    std::string path;
    const int row = grid_mode_
        ? ItemAtPoint(p)
        : (table_ != nullptr && table_->GetBounds().Contains(p)
                   && table_->RowAtY(p.y) >= 0 &&
                   table_->RowAtY(p.y) <
                       static_cast<int>(visible_.size())
               ? visible_[static_cast<std::size_t>(table_->RowAtY(p.y))]
               : -1);
    if (row >= 0) {
        if (row < static_cast<int>(entries_.size())) {
            // Right-click selects the row so the action's target is
            // visible — same rule as Premiere's project panel.
            selected_row_ = row;
            // Table shows a row index into visible_ — translate back.
            for (int i = 0; i < static_cast<int>(visible_.size()); ++i) {
                if (visible_[static_cast<std::size_t>(i)] == row) {
                    table_->SetSelectedRow(i);
                    break;
                }
            }
            add_btn_->SetEnabled(true);
            path = entries_[static_cast<std::size_t>(row)].path;
            if (hooks_.on_select) hooks_.on_select(path);
        }
    }

    std::vector<Widgets::ContextMenu::Item> items;
    Widgets::ContextMenu::Item open_source;
    open_source.label = "Open in Source Monitor";
    open_source.enabled = !path.empty() &&
                          hooks_.on_open_source != nullptr;
    open_source.action = [this, path] {
        if (hooks_.on_open_source) hooks_.on_open_source(path);
    };
    items.push_back(std::move(open_source));

    Widgets::ContextMenu::Item add;
    add.label = "Add to Timeline";
    add.enabled = !path.empty();
    add.action = [this, path] {
        if (hooks_.on_add_to_timeline) hooks_.on_add_to_timeline(path);
    };
    items.push_back(std::move(add));

    Widgets::ContextMenu::Item reveal;
    reveal.label = "Reveal in Explorer";
    reveal.enabled = !path.empty() && on_reveal_entry_ != nullptr;
    reveal.action = [this, path] {
        if (on_reveal_entry_) on_reveal_entry_(path);
    };
    items.push_back(std::move(reveal));

    Widgets::ContextMenu::Item remove;
    remove.label = "Remove from Bin";
    remove.enabled = !path.empty() && on_remove_entry_ != nullptr;
    remove.action = [this, path] {
        if (on_remove_entry_) on_remove_entry_(path);
    };
    items.push_back(std::move(remove));

    Widgets::ContextMenu::Item import;
    import.label = "Import Media…";
    import.separator_before = true;
    import.action = [this] {
        if (hooks_.on_import) hooks_.on_import();
    };
    items.push_back(std::move(import));

    Widgets::ContextMenu::Item beats;
    beats.label = "Detect Beats → Markers";
    beats.separator_before = true;
    beats.enabled = !path.empty();
    beats.action = [this, path] {
        if (hooks_.on_detect_beats) hooks_.on_detect_beats(path);
    };
    items.push_back(std::move(beats));

    Widgets::ContextMenu::Item transcribe;
    transcribe.label = "Transcribe → Markers (Whisper)";
    transcribe.enabled =
        !path.empty() &&
        (!hooks_.transcribe_available || hooks_.transcribe_available());
    transcribe.action = [this, path] {
        if (hooks_.on_transcribe) hooks_.on_transcribe(path);
    };
    items.push_back(std::move(transcribe));

    // Loaded audio-plugin DLLs — one bounce item per plugin effect.
    if (hooks_.audio_fx_list != nullptr) {
        bool first = true;
        for (const auto& [id, name] : hooks_.audio_fx_list()) {
            Widgets::ContextMenu::Item fx;
            fx.label = "Audio FX → " + name;
            fx.separator_before = first;
            fx.enabled = !path.empty() && hooks_.on_audio_fx != nullptr;
            fx.action = [this, path, id] {
                if (hooks_.on_audio_fx) hooks_.on_audio_fx(path, id);
            };
            items.push_back(std::move(fx));
            first = false;
        }
    }

    auto menu = Widgets::ContextMenu::Create(std::move(items));
    menu->SetAnchor(p);
    ShowOverlay(menu);
    return true;
}

bool MediaBinWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    using Type = XER::UI::Widgets::Event::Type;
    // Filter chips work in both grid and list modes.
    if (event.type == Type::Click) {
        for (int i = 0; i < 3; ++i) {
            if (ChipRect(i).Contains(event.position)) {
                filter_ = i;
                grid_scroll_ = 0.0f;
                RefreshRows();
                Invalidate();
                return true;
            }
        }
    }
    if (event.type == Type::MouseMove) {
        int chip = -1;
        for (int i = 0; i < 3; ++i) {
            if (ChipRect(i).Contains(event.position)) chip = i;
        }
        if (chip != chip_hovered_) {
            chip_hovered_ = chip;
            Invalidate();
        }
    }
    if (!grid_mode_) return false;
    const bool inside = grid_rect_.Contains(event.position);
    switch (event.type) {
        case Type::MouseMove: {
            const int h = inside ? ItemAtPoint(event.position) : -1;
            if (h != grid_hovered_) {
                grid_hovered_ = h;
                Invalidate();
            }
            return inside || chip_hovered_ >= 0;
        }
        case Type::MouseWheel: {
            if (!inside) return false;
            grid_scroll_ -= event.wheel_delta / 120.0f * 60.0f;
            grid_scroll_ = std::max(0.0f, grid_scroll_);
            Invalidate();
            return true;
        }
        case Type::Click: {
            if (!inside) return false;
            const int index = ItemAtPoint(event.position);
            selected_row_ = index;
            add_btn_->SetEnabled(index >= 0);
            if (index >= 0 &&
                index < static_cast<int>(entries_.size())) {
                const std::string& path =
                    entries_[static_cast<std::size_t>(index)].path;
                if (hooks_.on_select) hooks_.on_select(path);
                MaybeOpenSource(index);
            }
            Invalidate();
            return true;
        }
        case Type::MouseDown:
            // Right-button downs are context-menu requests — returning
            // false lets the tree's context-menu fallback route them to
            // ShowContextMenu instead of being swallowed by the grid.
            if (event.mouse_button ==
                XER::UI::Widgets::MouseButton::Right) {
                return false;
            }
            return inside;
        default:
            return inside;
    }
}

void MediaBinWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
