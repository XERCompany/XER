#include "media_bin_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "app_runtime/ui_manifest.h"
#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "icon_helpers.h"
#include "io/still_image.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/button_group.h"
#include "widgets/context_menu.h"
#include "widgets/label.h"
#include "widgets/select.h"
#include "widgets/table.h"
#include "widgets/text_input.h"

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

// Lowercased extension of a UTF-8 media path, no leading dot —
// StillImageHandlesExtension's documented input ("exr", "tiff", ...).
std::string MediaExtensionLower(const std::string& utf8_path) {
    std::string ext = XER::Helper::String::ToLowerAscii(
        XER::Helper::Path::FromUtf8(utf8_path).extension().string());
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    return ext;
}

// Still-image extensions (the image2/still-image tail of the shared import
// filter): a HasVideo() entry whose file extension is a raster is an
// Image tile, not a Video one — same classification the bin glyphs,
// the Kind column and the Kind sort all share.
bool IsStillMedia(const CatalogEntry& entry) {
    if (!entry.HasVideo()) return false;
    static const std::unordered_set<std::string> kStillExts = {
        "png",  "jpg",  "jpeg", "jpe", "bmp", "dib", "gif", "tif",
        "tiff", "webp", "tga",  "ppm", "pgm", "pbm", "pam", "xpm",
        "avif", "heic", "exr",  "dpx", "hdr", "psd", "pfm", "raw",
        "cr2",  "nef",  "arw",  "dng", "ora",
    };
    return kStillExts.count(MediaExtensionLower(entry.path)) != 0;
}

// Grouping rank for the Kind sort + tile glyph pick: Video first,
// then Image, then Audio (each group name-sorted inside).
int KindRank(const CatalogEntry& entry) {
    if (!entry.HasVideo()) return 2;
    return IsStillMedia(entry) ? 1 : 0;
}

std::string FormatKind(const CatalogEntry& entry) {
    if (!entry.HasVideo()) return "Audio";
    return IsStillMedia(entry) ? "Image" : "Video";
}

// Logical icon-pack name for the tile's kind badge (icons.yaml).
const char* KindIcon(const CatalogEntry& entry) {
    if (!entry.HasVideo()) return "bin.kind.audio";
    return IsStillMedia(entry) ? "bin.kind.image" : "bin.kind.video";
}

// Byte count -> "742 B" / "3.1 MB" / "12.4 GB" for the list column.
std::string FormatSize(std::int64_t bytes) {
    if (bytes <= 0) return "--";
    static const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[24];
    if (unit == 0) {
        std::snprintf(buffer, sizeof(buffer), "%d %s",
                      static_cast<int>(value), kUnits[unit]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value,
                      kUnits[unit]);
    }
    return buffer;
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

// Still-image decode results memoized by UTF-8 path — DrawGrid
// repaints on every hover/scroll, and decoding EXR/RAW per paint would
// thrash disk + CPU. Failures are cached too (a corrupt still must not
// retry every frame). Bounded: the whole map clears on overflow —
// entries are shared_ptrs so a clear can never dangle a reference a
// caller still holds.
constexpr std::size_t kStillThumbCacheMax = 64;

std::shared_ptr<const Io::Image> StillThumb(const std::string& utf8_path) {
    static std::unordered_map<std::string, std::shared_ptr<Io::Image>>
        cache;
    const auto it = cache.find(utf8_path);
    if (it != cache.end()) {
        return it->second;
    }
    if (cache.size() >= kStillThumbCacheMax) {
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
    // Icon-capable buttons resolve through the pack (idempotent).
    VeIcons::InstallButtonIconResolver();

    // assets/ui/strings.ini — media_bin_title overrides the header.
    title_ = std::make_shared<Widgets::Label>(
        AppRuntime::SharedUiStrings().Get("media_bin_title",
                                          "MEDIA BIN"));
    // Live substring filter over entry names/paths — every keystroke
    // rebuilds the visible set (RebuildVisible does kind filter +
    // search + sort in one pass).
    search_input_ = std::make_shared<Widgets::TextInput>("");
    search_input_->SetPlaceholder("Search media…");
    search_input_->SetTooltip("Filter by file name or path");
    search_input_->SetOnValueChanged([this](const std::string& value) {
        if (search_ == value) return;
        search_ = value;
        grid_scroll_ = 0.0f;
        RefreshRows();
    });
    import_btn_ = std::make_shared<Widgets::Button>(
        "Import", Widgets::ButtonStyle::Primary);
    import_btn_->SetIcon("bin.import");
    import_btn_->SetTooltip("Import media into the catalog");
    import_btn_->SetOnClick([this] {
        if (hooks_.on_import) hooks_.on_import();
    });
    add_btn_ = std::make_shared<Widgets::Button>(
        "Add to V1", Widgets::ButtonStyle::Secondary);
    add_btn_->SetIcon("bin.add_timeline");
    add_btn_->SetTooltip("Insert selected media at the playhead");
    add_btn_->SetOnClick([this] {
        const std::string path = SelectedPath();
        if (!path.empty() && hooks_.on_add_to_timeline) {
            hooks_.on_add_to_timeline(path);
        }
    });
    table_ = Widgets::Table::Create();
    table_->SetColumns({{"Name", 0.0f, false},
                        {"Kind", 56.0f, false},
                        {"Dur", 52.0f, false},
                        {"Size", 64.0f, false},
                        {"Info", 88.0f, false}});
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
    // Grid/List view toggle — a two-button icon ButtonGroup, same
    // joined-strip convention as the kind filter below.
    view_group_ = std::make_shared<Widgets::ButtonGroup>(
        Widgets::ButtonGroup::Orientation::Horizontal);
    grid_btn_ = std::make_shared<Widgets::Button>(
        "", Widgets::ButtonStyle::Primary);
    grid_btn_->SetIcon("bin.grid");
    grid_btn_->SetTooltip("Grid view");
    grid_btn_->SetOnClick([this] { SetView(true); });
    list_btn_ = std::make_shared<Widgets::Button>(
        "", Widgets::ButtonStyle::Secondary);
    list_btn_->SetIcon("bin.list");
    list_btn_->SetTooltip("List view");
    list_btn_->SetOnClick([this] { SetView(false); });
    view_group_->AddChild(grid_btn_);
    view_group_->AddChild(list_btn_);

    // Sort dropdown — index order IS the sort_ contract
    // (0 Name, 1 Duration, 2 Size, 3 Kind, 4 Imported); sort_ is a
    // plain member so the choice survives for the session.
    sort_select_ = std::make_shared<Widgets::Select>(
        std::vector<std::string>{"Name", "Duration", "Size", "Kind",
                                 "Imported"},
        sort_);
    sort_select_->SetTooltip("Sort the bin");
    sort_select_->SetOnChanged([this](int index) {
        if (index < 0 || index == sort_) return;
        sort_ = index;
        RefreshRows();
    });

    // Kind filter as a segmented strip — the lab_panel mode-strip
    // pattern: ButtonGroup joins the three buttons edge-to-edge, the
    // active filter keeps the Primary fill while the rest fall back
    // to Secondary (SyncFilterButtons).
    filter_group_ = std::make_shared<Widgets::ButtonGroup>(
        Widgets::ButtonGroup::Orientation::Horizontal);
    filter_all_btn_ = std::make_shared<Widgets::Button>(
        "All", Widgets::ButtonStyle::Primary);
    filter_all_btn_->SetTooltip("Show every catalog entry");
    filter_all_btn_->SetOnClick([this] { SetFilter(0); });
    filter_video_btn_ = std::make_shared<Widgets::Button>(
        "Video", Widgets::ButtonStyle::Secondary);
    filter_video_btn_->SetTooltip("Show only entries with video");
    filter_video_btn_->SetOnClick([this] { SetFilter(1); });
    filter_audio_btn_ = std::make_shared<Widgets::Button>(
        "Audio", Widgets::ButtonStyle::Secondary);
    filter_audio_btn_->SetTooltip("Show only audio entries");
    filter_audio_btn_->SetOnClick([this] { SetFilter(2); });
    filter_group_->AddChild(filter_all_btn_);
    filter_group_->AddChild(filter_video_btn_);
    filter_group_->AddChild(filter_audio_btn_);

    status_ = std::make_shared<Widgets::Label>("Import via button or File menu", true);

    AddChild(title_);
    AddChild(search_input_);
    AddChild(import_btn_);
    AddChild(add_btn_);
    AddChild(view_group_);
    AddChild(sort_select_);
    AddChild(filter_group_);
    AddChild(table_);
    AddChild(status_);

    SyncFilterButtons();
    SyncViewButtons();
}

void MediaBinWidget::SetEntries(std::vector<CatalogEntry> entries) {
    entries_ = std::move(entries);
    // A refresh may have removed entries (or the same path may now
    // resolve to changed media) — drop the peaks caches; ready paths
    // re-answer instantly from media_prep's own warm cache.
    peaks_cache_.clear();
    peaks_lru_.clear();
    peaks_missing_.clear();
    peaks_missing_lru_.clear();
    RefreshRows();
}

void MediaBinWidget::NotifyMediaChanged() {
    // Only permanent-miss bookkeeping resets — ready peaks stay valid
    // for the session. A path that failed before the media pipeline
    // landed real data gets one re-probe on the next paint.
    peaks_missing_.clear();
    peaks_missing_lru_.clear();
}

void MediaBinWidget::SetPreviewActive(bool /*active*/) {
    // The bin no longer embeds a source monitor — kept as a no-op so
    // callers don't need a feature check.
}

void MediaBinWidget::RebuildVisible() {
    visible_.clear();
    visible_.reserve(entries_.size());
    // Case-insensitive substring over the whole path (the basename is
    // a suffix of it) — one pass covers name and directory matches.
    const std::string needle =
        XER::Helper::String::ToLowerAscii(search_);
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const CatalogEntry& entry = entries_[static_cast<std::size_t>(i)];
        const bool keep = filter_ == 0 ||
            (filter_ == 1 && entry.HasVideo()) ||
            (filter_ == 2 && !entry.HasVideo() && entry.has_audio);
        if (!keep) continue;
        if (!needle.empty() &&
            XER::Helper::String::ToLowerAscii(entry.path)
                    .find(needle) == std::string::npos) {
            continue;
        }
        visible_.push_back(i);
    }
    // Sort — the dropdown's option order is the index contract:
    // 0 Name, 1 Duration, 2 Size, 3 Kind, 4 Imported. Every key
    // tie-breaks on name so reordering is stable and deterministic.
    const auto name_of = [this](int index) {
        return XER::Helper::String::ToLowerAscii(
            Basename(entries_[static_cast<std::size_t>(index)].path));
    };
    const auto entry_of = [this](int index) -> const CatalogEntry& {
        return entries_[static_cast<std::size_t>(index)];
    };
    const auto by_name = [&](int a, int b) {
        const std::string na = name_of(a);
        const std::string nb = name_of(b);
        if (na != nb) return na < nb;
        return entry_of(a).id < entry_of(b).id;
    };
    switch (sort_) {
        case 1:   // Duration — longest first (pro-bin convention).
            std::stable_sort(visible_.begin(), visible_.end(),
                             [&](int a, int b) {
                                 const double da =
                                     entry_of(a).duration_seconds;
                                 const double db =
                                     entry_of(b).duration_seconds;
                                 if (da != db) return da > db;
                                 return by_name(a, b);
                             });
            break;
        case 2:   // Size — largest first.
            std::stable_sort(visible_.begin(), visible_.end(),
                             [&](int a, int b) {
                                 const auto sa =
                                     entry_of(a).file_size_bytes;
                                 const auto sb =
                                     entry_of(b).file_size_bytes;
                                 if (sa != sb) return sa > sb;
                                 return by_name(a, b);
                             });
            break;
        case 3:   // Kind — Video, Image, Audio groups; name inside.
            std::stable_sort(visible_.begin(), visible_.end(),
                             [&](int a, int b) {
                                 const int ra = KindRank(entry_of(a));
                                 const int rb = KindRank(entry_of(b));
                                 if (ra != rb) return ra < rb;
                                 return by_name(a, b);
                             });
            break;
        case 4:   // Imported — newest first (added_at is seconds;
                      // id breaks same-second ties deterministically).
            std::stable_sort(visible_.begin(), visible_.end(),
                             [&](int a, int b) {
                                 const auto ta =
                                     entry_of(a).added_at_unix;
                                 const auto tb =
                                     entry_of(b).added_at_unix;
                                 if (ta != tb) return ta > tb;
                                 return entry_of(a).id > entry_of(b).id;
                             });
            break;
        default:  // 0 Name A->Z.
            std::stable_sort(visible_.begin(), visible_.end(), by_name);
            break;
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
                        FormatSize(entry.file_size_bytes),
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
    // In list mode an empty result hides the table so the panel's own
    // drawn empty-state hint (Render) is what the user sees.
    table_->SetVisible(!grid_mode_ && !visible_.empty());
    add_btn_->SetEnabled(selected_row_ >= 0);
    // Status footer: "12 item(s)" unfiltered, "5 of 12 items · video ·
    // \"clip\"" while a kind filter and/or search is narrowing the set.
    std::string status;
    if (entries_.empty()) {
        status = "Import media to begin";
    } else if (visible_.size() == entries_.size()) {
        status = std::to_string(entries_.size()) + " item(s)";
    } else {
        status = std::to_string(visible_.size()) + " of " +
                 std::to_string(entries_.size()) + " items";
    }
    if (!entries_.empty()) {
        if (filter_ == 1) status += " · video";
        if (filter_ == 2) status += " · audio";
        if (!search_.empty()) status += " · \"" + search_ + "\"";
    }
    status_->SetText(status);
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

void MediaBinWidget::SetFilter(int filter) {
    if (filter_ == filter) return;
    filter_ = filter;
    grid_scroll_ = 0.0f;
    if (children_attached_) SyncFilterButtons();
    RefreshRows();
}

void MediaBinWidget::SyncFilterButtons() {
    if (!children_attached_) return;
    namespace Widgets = XER::UI::Widgets;
    const auto style = [this](int index) {
        return filter_ == index ? Widgets::ButtonStyle::Primary
                                : Widgets::ButtonStyle::Secondary;
    };
    filter_all_btn_->SetStyle(style(0));
    filter_video_btn_->SetStyle(style(1));
    filter_audio_btn_->SetStyle(style(2));
}

void MediaBinWidget::SetView(bool grid) {
    if (grid_mode_ == grid) return;
    grid_mode_ = grid;
    if (children_attached_) {
        SyncViewButtons();
        table_->SetVisible(!grid_mode_ && !visible_.empty());
    }
    Invalidate();
}

void MediaBinWidget::SyncViewButtons() {
    if (!children_attached_) return;
    namespace Widgets = XER::UI::Widgets;
    grid_btn_->SetStyle(grid_mode_ ? Widgets::ButtonStyle::Primary
                                   : Widgets::ButtonStyle::Secondary);
    list_btn_->SetStyle(grid_mode_ ? Widgets::ButtonStyle::Secondary
                                   : Widgets::ButtonStyle::Primary);
}

const AudioPeaks* MediaBinWidget::PeaksFor(const std::string& path) {
    if (!hooks_.get_audio_peaks || path.empty()) return nullptr;
    if (peaks_missing_.count(path) != 0u) return nullptr;
    const auto it = peaks_cache_.find(path);
    if (it != peaks_cache_.end()) return it->second.get();
    auto fresh = std::make_shared<AudioPeaks>();
    switch (hooks_.get_audio_peaks(path, fresh.get())) {
        case PeaksLookup::kReady:
            // Same payload guard the timeline applies — a short/corrupt
            // min/max buffer must never reach the column math.
            if (fresh->buckets > 0 && fresh->channels > 0 &&
                fresh->data.size() /
                            (2u * static_cast<std::size_t>(
                                      fresh->channels)) >=
                    fresh->buckets) {
                peaks_cache_[path] = fresh;
                peaks_lru_.push_back(path);
                constexpr std::size_t kMaxPeaks = 48;
                while (peaks_lru_.size() > kMaxPeaks) {
                    peaks_cache_.erase(peaks_lru_.front());
                    peaks_lru_.pop_front();
                }
                return fresh.get();
            }
            peaks_missing_.insert(path);
            peaks_missing_lru_.push_back(path);
            break;
        case PeaksLookup::kPending:
            return nullptr;   // worker decoding — re-asked next frame
        case PeaksLookup::kFailed:
            peaks_missing_.insert(path);
            peaks_missing_lru_.push_back(path);
            break;
    }
    constexpr std::size_t kMaxMissing = 256;
    while (peaks_missing_lru_.size() > kMaxMissing) {
        peaks_missing_.erase(peaks_missing_lru_.front());
        peaks_missing_lru_.pop_front();
    }
    return nullptr;
}

void MediaBinWidget::DrawWaveStrip(
    XER::UI::Rendering::Renderer& renderer, const AudioPeaks& peaks,
    const XER::UI::Layout::Rect& strip,
    XER::UI::Theme::Color color) {
    if (strip.width < 8.0f || strip.height < 4.0f) return;
    const std::size_t buckets = peaks.buckets;
    const int channels = peaks.channels;
    const int columns = std::clamp(
        static_cast<int>(strip.width / 3.0f), 8, 96);
    const float bar_w = strip.width / static_cast<float>(columns);
    const float mid_y = strip.y + strip.height * 0.5f;
    for (int c = 0; c < columns; ++c) {
        const std::size_t b0 =
            std::min(buckets - 1,
                     static_cast<std::size_t>(c) * buckets /
                         static_cast<std::size_t>(columns));
        const std::size_t b1 = std::min(
            buckets,
            std::max(b0 + 1,
                     static_cast<std::size_t>(c + 1) * buckets /
                         static_cast<std::size_t>(columns)));
        float mn = 1.0f, mx = -1.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const std::size_t base =
                static_cast<std::size_t>(ch) * buckets * 2u;
            for (std::size_t b = b0; b < b1; ++b) {
                mn = std::min(mn, peaks.data[base + b * 2u]);
                mx = std::max(mx, peaks.data[base + b * 2u + 1u]);
            }
        }
        if (!(mn <= mx)) { mn = 0.0f; mx = 0.0f; }
        mn = std::clamp(mn, -1.0f, 1.0f);
        mx = std::clamp(mx, -1.0f, 1.0f);
        const float half =
            std::max(0.75f, (mx - mn) * 0.5f * strip.height * 0.5f);
        renderer.FillRect(
            {strip.x + static_cast<float>(c) * bar_w,
             mid_y - half,
             std::max(1.0f, bar_w - 1.0f), half * 2.0f},
            color, 0.0f);
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
        // Stills the poster pipeline couldn't cover (EXR/DPX/TIFF/RAW/
        // PSD/HDR): the still path reads the source file straight to
        // BGRA — DrawFrameBgra-ready.
        if (!drew_poster &&
            Io::StillImageHandlesExtension(
                MediaExtensionLower(entry.path))) {
            const std::shared_ptr<const Io::Image> img =
                StillThumb(entry.path);
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
            // Audio tiles: real min/max peaks across the thumb center
            // once the media-prep cache answers (Premiere's audio
            // thumbnail); the classic 5-bar glyph stays while the
            // decode is pending or has permanently missed.
            bool drew_wave = false;
            if (!entry.HasVideo()) {
                if (const AudioPeaks* peaks = PeaksFor(entry.path);
                    peaks != nullptr) {
                    DrawWaveStrip(
                        renderer, *peaks,
                        {thumb.x + 6.0f, thumb.y + 6.0f,
                         thumb.width - 12.0f, thumb.height - 12.0f},
                        VeTheme::kClipAudio);
                    drew_wave = true;
                }
            }
            if (!drew_wave) {
                const float bars[] = {3.0f, 7.0f, 11.0f, 7.0f, 3.0f};
                const float bcx = thumb.x + thumb.width * 0.5f;
                const float bcy = thumb.y + thumb.height * 0.5f;
                float bx = bcx - 8.0f;
                for (const float b : bars) {
                    renderer.FillRect(
                        {bx, bcy - b * 0.5f, 2.0f, b}, tokens.muted,
                        1.0f);
                    bx += 4.0f;
                }
            }
        }

        // Kind badge, top-left of the thumb — a translucent chip with
        // the pack glyph (film / image / audio) so card kinds read at
        // a glance even when every tile has a poster.
        const XER::UI::Layout::Rect kind_chip{
            thumb.x + 5.0f, thumb.y + 5.0f, 20.0f, 18.0f};
        renderer.FillRect(kind_chip, VeTheme::kOverlayChip,
                          tokens.radius_small);
        VeIcons::DrawIcon(renderer, KindIcon(entry), 12,
                          tokens.foreground, kind_chip);
        if (hovered && !selected) {
            renderer.FillRect(
                thumb, VeTheme::WithAlpha(tokens.hover, 0.25f),
                tokens.radius_medium);
        }
        if (selected) {
            renderer.FillRect(thumb,
                              VeTheme::WithAlpha(tokens.accent, 0.12f),
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
}

void MediaBinWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    // Search icon in the input's left gutter — outside the
    // TextInput's own rect so its fill can't paint over it.
    if (search_icon_rect_.width > 0.0f) {
        VeIcons::DrawIcon(renderer, "bin.search", 14, tokens.muted,
                          search_icon_rect_);
    }
    if (grid_mode_) DrawGrid(renderer, tokens);
    // Empty state (both modes — the list hides its table when the
    // visible set is empty, so this hint is what the user sees).
    if (visible_.empty()) {
        renderer.DrawText(
            entries_.empty() ? "No media — Import to begin"
                             : "No matches for the current filter",
            grid_rect_, {tokens.muted, tokens.font_small, 400});
    }
}

void MediaBinWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float row_h = 30.0f;
    const float inner_x = bounds.x + pad;
    const float inner_w = bounds.width - pad * 2.0f;

    title_->Layout({inner_x, bounds.y + pad * 0.6f, inner_w, 24.0f},
                   tokens);

    // Search row: pack icon in a left gutter, the TextInput takes the
    // rest (its fill would cover an icon drawn inside its own rect).
    const float search_y = bounds.y + pad * 0.6f + 24.0f + gap;
    const float search_h = VeTheme::kControlHeight;
    search_icon_rect_ = {inner_x + 2.0f,
                         search_y + (search_h - 14.0f) * 0.5f,
                         14.0f, 14.0f};
    search_input_->Layout(
        {inner_x + 22.0f, search_y, inner_w - 22.0f, search_h},
        tokens);

    // Actions row: Import + Add on the left, the grid/list icon group
    // flush right.
    const float button_y = search_y + search_h + gap;
    const float add_w = 100.0f;
    const float view_w = 56.0f;
    import_btn_->Layout({inner_x, button_y, 78.0f, row_h}, tokens);
    add_btn_->Layout({inner_x + 78.0f + gap, button_y, add_w, row_h},
                     tokens);
    view_group_->Layout(
        {inner_x + inner_w - view_w, button_y + 2.0f, view_w,
         row_h - 4.0f},
        tokens);

    // Filter chips + sort dropdown share the next strip — the chips
    // take what the dropdown leaves.
    const float strip_y = button_y + row_h + gap;
    const float strip_h = 26.0f;
    const float sort_w =
        std::min(118.0f, std::max(84.0f, inner_w * 0.36f));
    filter_group_->Layout(
        {inner_x, strip_y, inner_w - sort_w - gap, strip_h}, tokens);
    sort_select_->Layout(
        {inner_x + inner_w - sort_w, strip_y, sort_w, strip_h},
        tokens);

    // Content fills down to the status footer.
    const float footer_h = 18.0f;
    const float footer_y =
        bounds.y + bounds.height - pad - footer_h;
    const float list_top = strip_y + strip_h + gap;
    const float content_h =
        std::max(0.0f, footer_y - gap - list_top);
    const float table_h = std::max(80.0f, content_h);
    table_->SetVisible(!grid_mode_ && !visible_.empty());
    if (!grid_mode_) {
        table_->Layout({inner_x, list_top, inner_w, table_h}, tokens);
    }
    grid_rect_ = {inner_x, list_top, inner_w, table_h};
    status_->Layout({inner_x, footer_y, inner_w, footer_h}, tokens);
}

XER::UI::Layout::Rect MediaBinWidget::GridRect() const {
    return grid_rect_;
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

    Widgets::ContextMenu::Item import_url;
    import_url.label = "Import from URL…";
    import_url.enabled = hooks_.on_import_url != nullptr;
    import_url.action = [this] {
        if (hooks_.on_import_url) hooks_.on_import_url();
    };
    items.push_back(std::move(import_url));

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
    if (!grid_mode_) return false;
    const bool inside = grid_rect_.Contains(event.position);
    switch (event.type) {
        case Type::MouseMove: {
            // Drag promotion: past a small dead-zone the press becomes
            // a drag and the shell's hover hook starts tracking the
            // cursor (mouse capture keeps moves routed here even
            // outside the bin's bounds).
            if (drag_entry_ >= 0) {
                constexpr float kDragDeadZone = 6.0f;
                const float dx = event.position.x - drag_press_.x;
                const float dy = event.position.y - drag_press_.y;
                if (!drag_active_ &&
                    dx * dx + dy * dy >
                        kDragDeadZone * kDragDeadZone) {
                    drag_active_ = true;
                }
                if (drag_active_ && hooks_.on_drag_hover) {
                    hooks_.on_drag_hover(event.position, true);
                }
            }
            const int h = inside ? ItemAtPoint(event.position) : -1;
            if (h != grid_hovered_) {
                grid_hovered_ = h;
                Invalidate();
            }
            return inside || drag_entry_ >= 0;
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
            // Arm a possible card drag-out — promoted on the first
            // MouseMove past the dead-zone (see MouseMove above).
            drag_entry_ = inside ? ItemAtPoint(event.position) : -1;
            drag_press_ = event.position;
            drag_active_ = false;
            return inside;
        case Type::MouseUp: {
            if (event.mouse_button ==
                    XER::UI::Widgets::MouseButton::Left &&
                drag_entry_ >= 0) {
                const int entry = drag_entry_;
                const bool was_dragging = drag_active_;
                drag_entry_ = -1;
                drag_active_ = false;
                if (was_dragging) {
                    if (hooks_.on_drag_hover) {
                        hooks_.on_drag_hover(event.position, false);
                    }
                    // Released outside the bin = a drop offer; the
                    // shell decides what (if anything) sits under it.
                    if (!GetBounds().Contains(event.position) &&
                        hooks_.on_drop &&
                        entry < static_cast<int>(entries_.size())) {
                        hooks_.on_drop(
                            entries_[static_cast<std::size_t>(entry)]
                                .path,
                            event.position);
                    }
                }
                return true;
            }
            return inside;
        }
        default:
            return inside;
    }
}

void MediaBinWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
