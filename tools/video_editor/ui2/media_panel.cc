// MediaPanel — see media_panel.h for the contract. The panel owns
// only view state (search text, sort mode, kind filter, grid/list
// flag, path-keyed selection); every file/document action leaves
// through the Hooks bundle.
#include "media_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <utility>

#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/button_group.h"
#include "widgets/context_menu.h"
#include "widgets/label.h"
#include "widgets/select.h"
#include "widgets/text_input.h"
#include "widgets/virtual_list.h"

// windows.h may have re-defined DrawText before we got here.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
using XER::UI::Theme::Color;

// Toolbar metrics — the pro-NLE density row (Theme::kControlHeight)
// with the 4px grid gutter.
constexpr float kToolbarH = 30.0f;
constexpr float kToolbarGap = 6.0f;
constexpr float kStatusH = 18.0f;
constexpr float kListRowH = 30.0f;

std::string Basename(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string Lower(const std::string& text) {
    return XER::Helper::String::ToLowerAscii(text);
}

// Lowercased extension of a UTF-8 media path, no leading dot — the
// still-image classification the kind filter and Kind sort share.
std::string MediaExtensionLower(const std::string& utf8_path) {
    std::string ext = Lower(
        XER::Helper::Path::FromUtf8(utf8_path).extension().string());
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    return ext;
}

// Still-image extensions (the image2/still-image tail of the shared
// import filter): a HasVideo() entry whose extension is a raster is an
// Image tile, not a Video one — same rule the legacy bin ran.
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

// Grouping rank for the Kind sort: Video first, then Image, then Audio.
int KindRank(const CatalogEntry& entry) {
    if (!entry.HasVideo()) return 2;
    return IsStillMedia(entry) ? 1 : 0;
}

std::string FormatKind(const CatalogEntry& entry) {
    if (!entry.HasVideo()) return "Audio";
    return IsStillMedia(entry) ? "Image" : "Video";
}

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

}  // namespace

std::shared_ptr<MediaPanel> MediaPanel::Create() {
    // shared_ptr can't see the private ctor — the kit's Create()
    // convention works around it the same way.
    return std::shared_ptr<MediaPanel>(new MediaPanel());
}

MediaPanel::MediaPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void MediaPanel::SetHooks(Hooks hooks) {
    hooks_ = std::move(hooks);
    if (grid_ != nullptr) {
        grid_->get_poster = hooks_.get_poster;
        grid_->get_audio_peaks = hooks_.get_audio_peaks;
    }
}

void MediaPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    search_input_ = std::make_shared<W::TextInput>("");
    search_input_->SetPlaceholder("Search media…");
    search_input_->SetTooltip("Filter by file name or path");
    search_input_->SetOnValueChanged([this](const std::string& value) {
        if (search_ == value) return;
        search_ = value;
        RebuildVisible();
    });

    import_btn_ = std::make_shared<W::Button>(
        "Import", W::ButtonStyle::Primary);
    import_btn_->SetSize(W::ButtonSize::Small);
    import_btn_->SetTooltip("Import media into the catalog");
    import_btn_->SetOnClick([this] {
        if (hooks_.on_import) hooks_.on_import();
    });

    // Sort menu — a menu-button carrying the five sort keys with a
    // check glyph on the active one (spec: "sort menu").
    sort_btn_ = std::make_shared<W::Button>(
        "Sort: Name", W::ButtonStyle::Secondary);
    sort_btn_->SetSize(W::ButtonSize::Small);
    sort_btn_->SetTooltip("Sort the bin");
    const auto sort_items = [this] {
        static const char* kNames[] = {"Name", "Duration", "Size",
                                       "Kind", "Imported"};
        std::vector<W::ContextMenu::Item> items;
        for (int i = 0; i < 5; ++i) {
            W::ContextMenu::Item item;
            item.label = kNames[i];
            item.checked = static_cast<int>(sort_) == i;
            const SortMode mode = static_cast<SortMode>(i);
            item.action = [this, mode] { SetSortMode(mode); };
            items.push_back(std::move(item));
        }
        return items;
    };
    sort_btn_->SetMenu(sort_items());

    filter_select_ = std::make_shared<W::Select>(
        std::vector<std::string>{"All", "Video", "Audio"},
        static_cast<int>(filter_));
    filter_select_->SetTooltip("Filter by media kind");
    filter_select_->SetOnChanged([this](int index) {
        SetKindFilter(static_cast<KindFilter>(index));
    });

    view_group_ = std::make_shared<W::ButtonGroup>(
        W::ButtonGroup::Orientation::Horizontal);
    grid_btn_ = std::make_shared<W::Button>(
        "Grid", W::ButtonStyle::Secondary);
    grid_btn_->SetSize(W::ButtonSize::Small);
    grid_btn_->SetTooltip("Grid view");
    grid_btn_->SetOnClick([this] { SetGridMode(true); });
    list_btn_ = std::make_shared<W::Button>(
        "List", W::ButtonStyle::Secondary);
    list_btn_->SetSize(W::ButtonSize::Small);
    list_btn_->SetTooltip("List view");
    list_btn_->SetOnClick([this] { SetGridMode(false); });
    view_group_->AddChild(grid_btn_);
    view_group_->AddChild(list_btn_);

    // The S7 grid surface — virtualized cards in one paint pass.
    grid_ = std::make_shared<BinGridSurface>();
    grid_->get_poster = hooks_.get_poster;
    grid_->get_audio_peaks = hooks_.get_audio_peaks;
    grid_->SetOnSelectionChanged([this] { PullSelectionFromGrid(); });
    grid_->SetOnActivateItem([this](int index) {
        if (const CatalogEntry* e = EntryForVisible(index);
            e != nullptr && hooks_.on_open_source) {
            hooks_.on_open_source(e->path);
        }
    });
    grid_->SetOnContextMenu(
        [this](int index, Layout::Point p) { ShowCardMenu(index, p); });
    grid_->SetOnBeginDrag(
        [this](const std::vector<std::string>& paths, Layout::Point p) {
            if (hooks_.on_begin_drag) hooks_.on_begin_drag(paths, p);
        });
    grid_->SetOnDragUpdate([this](Layout::Point p) {
        if (hooks_.on_drag_update) hooks_.on_drag_update(p);
    });
    grid_->SetOnEndDrag([this](Layout::Point p) {
        if (hooks_.on_end_drag) hooks_.on_end_drag(p);
    });

    // List mode — one virtualized row per visible entry.
    list_ = W::VirtualList::Create();
    list_->SetItemHeight(kListRowH);
    list_->SetSelectionMode(W::VirtualList::SelectionMode::Multiple);
    list_->SetItemRenderer(
        [this](XER::UI::Rendering::Renderer& renderer,
               Layout::Rect row, int index, bool /*selected*/) {
            const CatalogEntry* e = EntryForVisible(index);
            if (e == nullptr) return;
            const auto& tokens = CachedLayoutTokens();
            const Color kind_color =
                !e->HasVideo()   ? Theme::kClipAudio
                : IsStillMedia(*e) ? Theme::kClipGenerated
                                   : Theme::kClipVideo;
            // Kind chip letter.
            renderer.DrawText(
                !e->HasVideo()   ? "A"
                : IsStillMedia(*e) ? "I"
                                   : "V",
                {row.x + 6.0f, row.y + 4.0f, 16.0f, row.height - 6.0f},
                {kind_color, Theme::kFontBody, 700});
            // Name, then a muted meta tail "dur · kind · size".
            std::string meta = FormatKind(*e);
            if (e->duration_seconds > 0.0) {
                char dur[16];
                const int m = static_cast<int>(e->duration_seconds / 60.0);
                const int s = static_cast<int>(e->duration_seconds) % 60;
                std::snprintf(dur, sizeof(dur), "%d:%02d", m, s);
                meta = std::string(dur) + " · " + meta;
            }
            if (e->file_size_bytes > 0) {
                meta += " · " + FormatSize(e->file_size_bytes);
            }
            if (e->proxy_state == 1) meta += " · PX..";
            else if (e->proxy_state == 2) meta += " · PX";
            else if (e->proxy_state == 3) meta += " · PX!";
            renderer.DrawText(
                Basename(e->path),
                {row.x + 24.0f, row.y + 4.0f, row.width - 30.0f,
                 row.height * 0.55f},
                {tokens.foreground, Theme::kFontBody, 500});
            renderer.DrawText(
                meta,
                {row.x + 24.0f, row.y + row.height * 0.5f,
                 row.width - 30.0f, row.height * 0.5f - 2.0f},
                {tokens.muted, Theme::kFontCaption, 400});
        });
    list_->SetOnSelectionChanged([this] { PullSelectionFromList(); });
    list_->SetOnItemActivated([this](int index) {
        if (const CatalogEntry* e = EntryForVisible(index);
            e != nullptr && hooks_.on_open_source) {
            hooks_.on_open_source(e->path);
        }
    });
    list_->SetVisible(false);

    status_ = std::make_shared<W::Label>("Import media to begin", true);

    AddChild(search_input_);
    AddChild(import_btn_);
    AddChild(sort_btn_);
    AddChild(filter_select_);
    AddChild(view_group_);
    AddChild(grid_);
    AddChild(list_);
    AddChild(status_);
    SyncViewButtons();
}

// ---- data -----------------------------------------------------------

void MediaPanel::SetEntries(std::vector<CatalogEntry> entries) {
    entries_ = std::move(entries);
    RebuildVisible();
}

void MediaPanel::NotifyMediaChanged() {
    if (grid_ != nullptr) grid_->NotifyMediaChanged();
}

void MediaPanel::SetStatus(const std::string& text, bool is_error) {
    if (status_ != nullptr) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

// ---- view state -------------------------------------------------------

void MediaPanel::SetGridMode(bool grid) {
    if (grid_mode_ == grid) return;
    grid_mode_ = grid;
    if (grid_ != nullptr) grid_->SetVisible(grid_mode_);
    if (list_ != nullptr) list_->SetVisible(!grid_mode_);
    SyncViewButtons();
    RequestLayout();
    Invalidate();
}

void MediaPanel::SetSearchText(std::string text) {
    if (search_input_ != nullptr) search_input_->SetValue(text);
    if (search_ == text) return;
    search_ = std::move(text);
    RebuildVisible();
}

void MediaPanel::SetSortMode(SortMode mode) {
    if (sort_ == mode) return;
    sort_ = mode;
    static const char* kNames[] = {"Name", "Duration", "Size", "Kind",
                                   "Imported"};
    if (sort_btn_ != nullptr) {
        sort_btn_->SetText(std::string("Sort: ") +
                           kNames[static_cast<int>(sort_)]);
        std::vector<W::ContextMenu::Item> items;
        for (int i = 0; i < 5; ++i) {
            W::ContextMenu::Item item;
            item.label = kNames[i];
            item.checked = static_cast<int>(sort_) == i;
            const SortMode m = static_cast<SortMode>(i);
            item.action = [this, m] { SetSortMode(m); };
            items.push_back(std::move(item));
        }
        sort_btn_->SetMenu(std::move(items));
    }
    RebuildVisible();
}

void MediaPanel::SetKindFilter(KindFilter filter) {
    if (filter_ == filter) return;
    filter_ = filter;
    if (filter_select_ != nullptr) {
        filter_select_->SetSelectedIndex(static_cast<int>(filter_));
    }
    RebuildVisible();
}

std::vector<std::string> MediaPanel::VisiblePaths() const {
    std::vector<std::string> out;
    out.reserve(visible_.size());
    for (const int index : visible_) {
        out.push_back(entries_[static_cast<std::size_t>(index)].path);
    }
    return out;
}

std::vector<std::string> MediaPanel::SelectedPaths() const {
    return {selected_paths_.begin(), selected_paths_.end()};
}

const CatalogEntry* MediaPanel::EntryForVisible(int visible_index) const {
    if (visible_index < 0 ||
        visible_index >= static_cast<int>(visible_.size())) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(
        visible_[static_cast<std::size_t>(visible_index)])];
}

// ---- filter / sort -----------------------------------------------------

void MediaPanel::RebuildVisible() {
    visible_.clear();
    visible_.reserve(entries_.size());
    // Case-insensitive substring over the whole path (the basename is a
    // suffix of it) — one pass covers name and directory matches.
    const std::string needle = Lower(search_);
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const CatalogEntry& entry = entries_[static_cast<std::size_t>(i)];
        const bool keep =
            filter_ == KindFilter::All ||
            (filter_ == KindFilter::Video && entry.HasVideo()) ||
            (filter_ == KindFilter::Audio && !entry.HasVideo() &&
             entry.has_audio);
        if (!keep) continue;
        if (!needle.empty() &&
            Lower(entry.path).find(needle) == std::string::npos) {
            continue;
        }
        visible_.push_back(i);
    }
    // Sort — every key tie-breaks on name (id on ties) so ordering is
    // stable and deterministic.
    const auto name_of = [this](int index) {
        return Lower(Basename(entries_[static_cast<std::size_t>(index)].path));
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
        case SortMode::Duration:   // longest first (pro-bin convention).
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
        case SortMode::Size:       // largest first.
            std::stable_sort(visible_.begin(), visible_.end(),
                             [&](int a, int b) {
                                 if (entry_of(a).file_size_bytes !=
                                     entry_of(b).file_size_bytes) {
                                     return entry_of(a).file_size_bytes >
                                            entry_of(b).file_size_bytes;
                                 }
                                 return by_name(a, b);
                             });
            break;
        case SortMode::Kind:       // Video, Image, Audio groups.
            std::stable_sort(visible_.begin(), visible_.end(),
                             [&](int a, int b) {
                                 const int ra = KindRank(entry_of(a));
                                 const int rb = KindRank(entry_of(b));
                                 if (ra != rb) return ra < rb;
                                 return by_name(a, b);
                             });
            break;
        case SortMode::Imported:   // newest first.
            std::stable_sort(visible_.begin(), visible_.end(),
                             [&](int a, int b) {
                                 if (entry_of(a).added_at_unix !=
                                     entry_of(b).added_at_unix) {
                                     return entry_of(a).added_at_unix >
                                            entry_of(b).added_at_unix;
                                 }
                                 return entry_of(a).id > entry_of(b).id;
                             });
            break;
        case SortMode::Name:
        default:
            std::stable_sort(visible_.begin(), visible_.end(), by_name);
            break;
    }
    // Drop selections whose paths left the visible set.
    for (auto it = selected_paths_.begin();
         it != selected_paths_.end();) {
        bool still_there = false;
        for (const int index : visible_) {
            if (entries_[static_cast<std::size_t>(index)].path == *it) {
                still_there = true;
                break;
            }
        }
        if (!still_there) {
            it = selected_paths_.erase(it);
        } else {
            ++it;
        }
    }

    // Push the card set to the grid surface.
    if (grid_ != nullptr) {
        std::vector<BinItem> items;
        items.reserve(visible_.size());
        for (const int index : visible_) {
            const CatalogEntry& e =
                entries_[static_cast<std::size_t>(index)];
            BinItem item;
            item.path = e.path;
            item.name = Basename(e.path);
            item.duration_seconds = e.duration_seconds;
            item.has_video = e.HasVideo();
            item.has_audio = e.has_audio;
            item.is_still = IsStillMedia(e);
            item.proxy_state = e.proxy_state;
            item.poster_path = e.poster_path;
            item.file_size_bytes = e.file_size_bytes;
            item.fps = e.fps;
            item.width = e.width;
            item.height = e.height;
            items.push_back(std::move(item));
        }
        grid_->SetItems(std::move(items));
    }
    if (list_ != nullptr) {
        list_->SetItemCount(static_cast<int>(visible_.size()));
    }
    PushSelectionToViews();
    SyncStatusText();
    Invalidate();
}

void MediaPanel::PushSelectionToViews() {
    if (grid_ != nullptr) {
        std::set<int> indices;
        for (int i = 0; i < static_cast<int>(visible_.size()); ++i) {
            if (selected_paths_.count(
                    entries_[static_cast<std::size_t>(
                                 visible_[static_cast<std::size_t>(i)])]
                        .path)) {
                indices.insert(i);
            }
        }
        grid_->SetSelectedIndices(std::move(indices));
    }
    if (list_ != nullptr) {
        std::set<int> indices;
        for (int i = 0; i < static_cast<int>(visible_.size()); ++i) {
            if (selected_paths_.count(
                    entries_[static_cast<std::size_t>(
                                 visible_[static_cast<std::size_t>(i)])]
                        .path)) {
                indices.insert(i);
            }
        }
        list_->SetSelectedIndices(std::move(indices));
    }
}

void MediaPanel::PullSelectionFromGrid() {
    if (grid_ == nullptr) return;
    std::set<std::string> next;
    for (const int index : grid_->SelectedIndices()) {
        if (const CatalogEntry* e = EntryForVisible(index)) {
            next.insert(e->path);
        }
    }
    if (next != selected_paths_) {
        selected_paths_ = std::move(next);
        PushSelectionToViews();   // keeps the hidden list in sync
        if (hooks_.on_select) hooks_.on_select(SelectedPaths());
    }
}

void MediaPanel::PullSelectionFromList() {
    if (list_ == nullptr) return;
    std::set<std::string> next;
    for (const int index : list_->GetSelectedIndices()) {
        if (const CatalogEntry* e = EntryForVisible(index)) {
            next.insert(e->path);
        }
    }
    if (next != selected_paths_) {
        selected_paths_ = std::move(next);
        PushSelectionToViews();
        if (hooks_.on_select) hooks_.on_select(SelectedPaths());
    }
}

void MediaPanel::SyncViewButtons() {
    if (grid_btn_ != nullptr) {
        grid_btn_->SetStyle(grid_mode_ ? W::ButtonStyle::Primary
                                       : W::ButtonStyle::Secondary);
    }
    if (list_btn_ != nullptr) {
        list_btn_->SetStyle(!grid_mode_ ? W::ButtonStyle::Primary
                                        : W::ButtonStyle::Secondary);
    }
    if (grid_ != nullptr) grid_->SetVisible(grid_mode_);
    if (list_ != nullptr) list_->SetVisible(!grid_mode_);
}

void MediaPanel::SyncStatusText() {
    if (status_ == nullptr) return;
    std::string text;
    if (entries_.empty()) {
        text = "Import media to begin";
    } else if (visible_.size() == entries_.size()) {
        text = std::to_string(entries_.size()) + " item(s)";
    } else {
        text = std::to_string(visible_.size()) + " of " +
               std::to_string(entries_.size()) + " items";
    }
    status_->SetText(text);
}

// ---- context menu -------------------------------------------------------

int MediaPanel::ListIndexAt(Layout::Point point) const {
    if (list_ == nullptr) return -1;
    const Layout::Rect b = list_->GetBounds();
    if (!b.Contains(point)) return -1;
    const int row = static_cast<int>(
        (point.y - b.y + list_->GetScrollY()) / kListRowH);
    return (row >= 0 && row < static_cast<int>(visible_.size()))
               ? row
               : -1;
}

bool MediaPanel::ShowContextMenuAt(Layout::Point point) {
    EnsureChildren();
    if (grid_mode_ && grid_ != nullptr && grid_->GetBounds().Contains(point)) {
        ShowCardMenu(grid_->ItemAtPoint(point), point);
        return true;
    }
    if (!grid_mode_ && list_ != nullptr &&
        list_->GetBounds().Contains(point)) {
        ShowCardMenu(ListIndexAt(point), point);
        return true;
    }
    return false;
}

void MediaPanel::ShowCardMenu(int visible_index, Layout::Point point) {
    EnsureChildren();
    const CatalogEntry* entry = EntryForVisible(visible_index);
    std::vector<W::ContextMenu::Item> items;
    if (entry != nullptr) {
        const std::string path = entry->path;
        W::ContextMenu::Item open;
        open.label = "Open in Source Monitor";
        open.action = [this, path] {
            if (hooks_.on_open_source) hooks_.on_open_source(path);
        };
        items.push_back(std::move(open));

        W::ContextMenu::Item add;
        add.label = "Add to Timeline";
        add.action = [this, path] {
            if (hooks_.on_add_to_timeline) {
                hooks_.on_add_to_timeline(path);
            }
        };
        items.push_back(std::move(add));

        if (entry->HasVideo()) {
            W::ContextMenu::Item px;
            const bool busy = entry->proxy_state == 1;
            px.label = busy    ? "Cancel Proxy"
                       : entry->proxy_state == 2 ? "Rebuild Proxy"
                       : entry->proxy_state == 3 ? "Retry Proxy"
                                                 : "Create Proxy";
            px.separator_before = true;
            px.action = [this, path, busy] {
                if (busy) {
                    if (hooks_.on_cancel_proxy) {
                        hooks_.on_cancel_proxy(path);
                    }
                } else if (hooks_.on_create_proxy) {
                    hooks_.on_create_proxy(path);
                }
            };
            items.push_back(std::move(px));
        }

        W::ContextMenu::Item remove;
        remove.label = selected_paths_.size() > 1
                           ? "Remove Selected from Bin"
                           : "Remove from Bin";
        remove.danger = true;
        remove.separator_before = true;
        remove.action = [this] {
            if (hooks_.on_remove_entries) {
                hooks_.on_remove_entries(SelectedPaths());
            }
        };
        items.push_back(std::move(remove));

        W::ContextMenu::Item reveal;
        reveal.label = "Reveal in Explorer";
        reveal.action = [this, path] {
            if (hooks_.on_reveal_entry) hooks_.on_reveal_entry(path);
        };
        items.push_back(std::move(reveal));
    }
    W::ContextMenu::Item import;
    import.label = "Import…";
    import.separator_before = entry != nullptr;
    import.action = [this] {
        if (hooks_.on_import) hooks_.on_import();
    };
    items.push_back(std::move(import));
    if (hooks_.on_import_url) {
        W::ContextMenu::Item url;
        url.label = "Import from URL…";
        url.action = [this] {
            if (hooks_.on_import_url) hooks_.on_import_url();
        };
        items.push_back(std::move(url));
    }
    auto menu = W::ContextMenu::Create(std::move(items));
    menu->SetAnchor(point);
    ShowOverlay(menu);
}

// ---- widget contract ------------------------------------------------------

void MediaPanel::Render(XER::UI::Rendering::Renderer& renderer,
                        const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void MediaPanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const float x = bounds.x + Theme::kSpaceSm;
    float y = bounds.y + Theme::kSpaceSm;
    const float w = bounds.width - Theme::kSpaceSm * 2.0f;
    const auto& tokens = CachedLayoutTokens();

    // Row 1: search (flex) + import.
    const float import_w = 64.0f;
    search_input_->Layout({x, y, w - import_w - kToolbarGap, kToolbarH},
                          tokens);
    import_btn_->Layout(
        {x + w - import_w, y, import_w, kToolbarH}, tokens);
    y += kToolbarH + kToolbarGap;

    // Row 2: kind filter + sort menu + grid/list toggle.
    const float filter_w = 74.0f;
    const float toggle_w = 96.0f;
    filter_select_->Layout({x, y, filter_w, kToolbarH}, tokens);
    view_group_->Layout(
        {x + w - toggle_w, y, toggle_w, kToolbarH}, tokens);
    sort_btn_->Layout(
        {x + filter_w + kToolbarGap, y,
         w - filter_w - toggle_w - kToolbarGap * 2.0f, kToolbarH},
        tokens);
    y += kToolbarH + kToolbarGap;

    // Content: grid surface or virtual list; status strip at the foot.
    const float content_h =
        bounds.y + bounds.height - Theme::kSpaceSm - kStatusH - y;
    const Layout::Rect content{x, y, w, std::max(0.0f, content_h)};
    grid_->Layout(content, tokens);
    list_->Layout(content, tokens);
    status_->Layout(
        {x, y + std::max(0.0f, content_h) + 2.0f, w, kStatusH}, tokens);
}

void MediaPanel::Tick(double /*dt_seconds*/) {
    // Poster/peaks arrivals repaint through NotifyMediaChanged — the
    // shell drains the worker's dirty flags and calls in.
}

}  // namespace XER::VideoEditor::Ui2
