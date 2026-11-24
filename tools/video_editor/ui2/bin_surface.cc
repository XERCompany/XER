// BinGridSurface — see bin_surface.h for the contract. All painting
// and hit zones live in content-space DIP coordinates (surface_hooks.h
// rule 6); the only document-facing outputs are the hook callbacks.
#include "bin_surface.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "media_catalog.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "video/frame.h"

// video_engine.h pulls windows.h, which re-defines DrawText as
// DrawTextA/W after renderer.h already undid it.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;

// Grid metrics — the legacy media-bin grid's verified numbers
// (ui/media_bin_widget.cc DrawGrid): ~132px card pitch, 8px gutters,
// 16:9 thumbs, meta strip underneath.
constexpr float kGap = 8.0f;
constexpr float kCardPitch = 132.0f;    // target card width incl. margin slack
constexpr float kMetaHeight = 24.0f;    // text strip under the thumb
constexpr float kInset = 8.0f;          // grid inner margin

// Floating scrollbar — same 8px rail / 2px pad / 14px hit strip the
// VirtualList and ScrollArea draw.
constexpr float kBarWidth = 8.0f;
constexpr float kBarPad = 2.0f;
constexpr float kHitWidth = 14.0f;

// Drag-out promotion threshold (DIP) — the same "few px before a press
// becomes a drag" budget the legacy bin used.
constexpr float kDragThreshold = 6.0f;

// Double-click synthesis budget.
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(400);
constexpr float kDoubleClickRadius = 6.0f;

// Bounded poster/peaks caches — the same budget the timeline surface
// uses; an eviction only drops paint-time data the lookup can refill.
constexpr std::size_t kPosterCacheMax = 96;
constexpr std::size_t kFailedCacheMax = 256;
constexpr std::size_t kPeaksCacheMax = 48;

std::string FormatCardDuration(double seconds) {
    if (seconds <= 0.0 || !std::isfinite(seconds)) return "--:--";
    const int minutes = static_cast<int>(seconds / 60.0);
    const int whole = static_cast<int>(seconds) % 60;
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, whole);
    return buffer;
}

bool IsValidBgraFrame(const video::Frame& frame) {
    return frame.IsValid() && frame.Format() == video::PixelFormat::BGRA8 &&
           frame.Width() > 0 && frame.Height() > 0 &&
           frame.Data() != nullptr &&
           frame.DataSize() >=
               static_cast<std::size_t>(frame.Width()) *
                   static_cast<std::size_t>(frame.Height()) * 4u;
}

void TouchLru(std::deque<std::string>* lru, const std::string& key) {
    const auto it = std::find(lru->begin(), lru->end(), key);
    if (it != lru->end()) lru->erase(it);
    lru->push_back(key);
}

}  // namespace

BinGridSurface::BinGridSurface() {
    SetFocusable(true);
}

// ---- model ---------------------------------------------------------

void BinGridSurface::SetItems(std::vector<BinItem> items) {
    // Keep index selection coherent across re-feeds: re-map by path so
    // a sort/filter from the panel preserves the user's selection.
    std::set<std::string> kept;
    for (const int index : selected_) {
        if (index >= 0 && index < static_cast<int>(items_.size())) {
            kept.insert(items_[static_cast<std::size_t>(index)].path);
        }
    }
    items_ = std::move(items);
    selected_.clear();
    anchor_ = -1;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        if (kept.count(items_[static_cast<std::size_t>(i)].path)) {
            selected_.insert(i);
        }
    }
    if (hovered_ >= static_cast<int>(items_.size())) hovered_ = -1;
    scroll_ = std::clamp(scroll_, 0.0f, MaxScrollY());
    Invalidate();
}

const BinItem* BinGridSurface::Item(int index) const {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return nullptr;
    }
    return &items_[static_cast<std::size_t>(index)];
}

void BinGridSurface::NotifyMediaChanged() {
    // The shell's async seam signalled fresh data — retry any
    // permanent misses (a path can fail before the pipeline lands it)
    // and keep warm hits (they stay valid for the session).
    poster_failed_.clear();
    poster_failed_lru_.clear();
    peaks_failed_.clear();
    peaks_failed_lru_.clear();
    Invalidate();
}

// ---- selection -------------------------------------------------------

void BinGridSurface::SetSelectedIndices(std::set<int> indices) {
    std::set<int> clean;
    for (const int index : indices) {
        if (index >= 0 && index < static_cast<int>(items_.size())) {
            clean.insert(index);
        }
    }
    selected_ = std::move(clean);
    anchor_ = selected_.empty() ? -1 : *selected_.rbegin();
    Invalidate();
}

std::vector<std::string> BinGridSurface::SelectedPaths() const {
    std::vector<std::string> out;
    out.reserve(selected_.size());
    for (const int index : selected_) {
        if (const BinItem* item = Item(index)) out.push_back(item->path);
    }
    return out;
}

void BinGridSurface::SetOnSelectionChanged(std::function<void()> cb) {
    on_selection_changed_ = std::move(cb);
}

void BinGridSurface::SetOnActivateItem(std::function<void(int)> cb) {
    on_activate_item_ = std::move(cb);
}

void BinGridSurface::SetOnContextMenu(
    std::function<void(int, Layout::Point)> cb) {
    on_context_menu_ = std::move(cb);
}

void BinGridSurface::SetOnBeginDrag(
    std::function<void(const std::vector<std::string>&,
                       Layout::Point)> cb) {
    on_begin_drag_ = std::move(cb);
}

void BinGridSurface::SetOnDragUpdate(
    std::function<void(Layout::Point)> cb) {
    on_drag_update_ = std::move(cb);
}

void BinGridSurface::SetOnEndDrag(
    std::function<void(Layout::Point)> cb) {
    on_end_drag_ = std::move(cb);
}

// ---- geometry ---------------------------------------------------------

int BinGridSurface::Columns() const {
    const float usable = GetBounds().width - kInset * 2.0f + kGap;
    return std::max(1, static_cast<int>(usable / kCardPitch));
}

int BinGridSurface::Rows() const {
    if (items_.empty()) return 0;
    return (ItemCount() + Columns() - 1) / Columns();
}

Layout::Size BinGridSurface::CardSize() const {
    const int cols = Columns();
    const float usable = GetBounds().width - kInset * 2.0f;
    const float card_w = (usable - kGap * static_cast<float>(cols - 1)) /
                         static_cast<float>(cols);
    return {card_w, card_w * 0.5625f + kMetaHeight};
}

Layout::Rect BinGridSurface::CardRect(int index) const {
    if (index < 0 || index >= ItemCount()) return {};
    const int cols = Columns();
    const Layout::Size size = CardSize();
    const int row = index / cols;
    const int col = index % cols;
    const Layout::Rect b = GetBounds();
    return {b.x + kInset + static_cast<float>(col) * (size.width + kGap),
            b.y + kInset + static_cast<float>(row) *
                                   (size.height + kGap) - scroll_,
            size.width, size.height};
}

int BinGridSurface::ItemAtPoint(Layout::Point point) const {
    // Column math in content space: which pitch column the x lands in,
    // then whether the point is inside a card (not the gutter).
    const Layout::Rect b = GetBounds();
    if (point.x < b.x + kInset || point.x >= b.x + b.width - kInset) {
        return -1;
    }
    const int cols = Columns();
    const Layout::Size size = CardSize();
    const float rel_x = point.x - (b.x + kInset);
    const float rel_y = point.y - (b.y + kInset) + scroll_;
    const int col = static_cast<int>(rel_x / (size.width + kGap));
    const int row = static_cast<int>(rel_y / (size.height + kGap));
    if (col < 0 || col >= cols || row < 0) return -1;
    // Gutter hits count as empty space (marquee start, not a card).
    const float in_x = rel_x - static_cast<float>(col) * (size.width + kGap);
    const float in_y = rel_y - static_cast<float>(row) * (size.height + kGap);
    if (in_x >= size.width || in_y >= size.height) return -1;
    const int index = row * cols + col;
    return index < ItemCount() ? index : -1;
}

std::pair<int, int> BinGridSurface::VisibleIndexRange() const {
    if (items_.empty()) return {0, 0};
    const int cols = Columns();
    const Layout::Size size = CardSize();
    const Layout::Rect b = GetBounds();
    const float top = scroll_ - kInset;
    const float bottom = scroll_ + b.height;
    const float pitch = size.height + kGap;
    const int first_row =
        std::max(0, static_cast<int>(top / pitch));
    const int last_row =
        std::min(Rows(), static_cast<int>(bottom / pitch) + 1);
    return {std::min(ItemCount(), first_row * cols),
            std::min(ItemCount(), last_row * cols)};
}

float BinGridSurface::GetContentHeight() const {
    const Layout::Size size = CardSize();
    return static_cast<float>(Rows()) * (size.height + kGap) + kInset * 2.0f;
}

float BinGridSurface::MaxScrollY() const {
    return std::max(0.0f, GetContentHeight() - GetBounds().height);
}

void BinGridSurface::ScrollTo(float y) {
    const float clamped = std::clamp(y, 0.0f, MaxScrollY());
    if (clamped == scroll_) return;
    scroll_ = clamped;
    Invalidate();
}

void BinGridSurface::EnsureVisible(int index) {
    const Layout::Rect card = CardRect(index);
    const Layout::Rect b = GetBounds();
    if (card.y < b.y) {
        ScrollTo(scroll_ - (b.y - card.y));
    } else if (card.y + card.height > b.y + b.height) {
        ScrollTo(scroll_ + (card.y + card.height) - (b.y + b.height));
    }
}

// ---- async media --------------------------------------------------------

const video::Frame* BinGridSurface::PosterFor(const BinItem& item,
                                              double at_seconds) {
    if (item.path.empty()) return nullptr;
    // Composite cache key — hover-scrub buckets get their own slots
    // so re-hovering a spot reuses the decoded frame; the mid-clip
    // default is just one more bucket.
    const std::string key =
        item.path + "@" + std::to_string(at_seconds);
    if (const auto it = posters_.find(key); it != posters_.end()) {
        TouchLru(&posters_lru_, key);
        return it->second.frame.get();
    }
    if (poster_failed_.count(key)) return nullptr;
    if (!get_poster) {
        // Unwired seam — treat as a permanent miss so paint never
        // stalls on a hook that will never answer.
        return nullptr;
    }
    auto frame = std::make_shared<video::Frame>();
    switch (get_poster(item.path, at_seconds, frame.get())) {
        case MediaLookup::kReady:
            if (IsValidBgraFrame(*frame)) {
                if (posters_.size() >= kPosterCacheMax) {
                    posters_.erase(posters_lru_.front());
                    posters_lru_.pop_front();
                }
                posters_.emplace(key, PosterEntry{std::move(frame)});
                posters_lru_.push_back(key);
                return posters_.at(key).frame.get();
            }
            [[fallthrough]];   // invalid payload counts as a miss
        case MediaLookup::kFailed:
            if (poster_failed_.size() >= kFailedCacheMax) {
                poster_failed_.erase(poster_failed_lru_.front());
                poster_failed_lru_.pop_front();
            }
            poster_failed_.insert(key);
            poster_failed_lru_.push_back(key);
            return nullptr;
        case MediaLookup::kPending:
        default:
            return nullptr;   // ask again next paint — never block
    }
}

double BinGridSurface::ScrubSeconds(const BinItem& item) const {
    // Bucket center — the card maps [0,1) onto kScrubBuckets slices.
    return item.duration_seconds *
           (static_cast<double>(hover_bucket_) + 0.5) / kScrubBuckets;
}

double BinGridSurface::PosterSeconds(int index) const {
    const BinItem* item = Item(index);
    if (item == nullptr) return 0.0;
    // Hover-scrub applies to video with a known duration; stills and
    // zero-duration items keep the catalog's mid-clip convention
    // (ExtractPoster duration*0.5, mid-stream when out of range).
    if (index == hovered_ && hover_bucket_ >= 0 && item->has_video &&
        !item->is_still && item->duration_seconds > 0.0) {
        return ScrubSeconds(*item);
    }
    return item->duration_seconds > 0.0
               ? item->duration_seconds * 0.5
               : 0.0;
}

const AudioPeaks* BinGridSurface::PeaksFor(const BinItem& item) {
    if (item.path.empty() || !get_audio_peaks) return nullptr;
    if (const auto it = peaks_.find(item.path); it != peaks_.end()) {
        TouchLru(&peaks_lru_, item.path);
        return it->second.get();
    }
    if (peaks_failed_.count(item.path)) return nullptr;
    AudioPeaks peaks;
    switch (get_audio_peaks(item.path, &peaks)) {
        case MediaLookup::kReady:
            if (peaks.channels > 0 && peaks.buckets > 0) {
                if (peaks_.size() >= kPeaksCacheMax) {
                    peaks_.erase(peaks_lru_.front());
                    peaks_lru_.pop_front();
                }
                auto stored = std::make_shared<AudioPeaks>(std::move(peaks));
                peaks_.emplace(item.path, stored);
                peaks_lru_.push_back(item.path);
                return stored.get();
            }
            [[fallthrough]];
        case MediaLookup::kFailed:
            if (peaks_failed_.size() >= kFailedCacheMax) {
                peaks_failed_.erase(peaks_failed_lru_.front());
                peaks_failed_lru_.pop_front();
            }
            peaks_failed_.insert(item.path);
            peaks_failed_lru_.push_back(item.path);
            return nullptr;
        case MediaLookup::kPending:
        default:
            return nullptr;
    }
}

// ---- painting ---------------------------------------------------------

void BinGridSurface::DrawCard(Rendering::Renderer& renderer,
                              const XER::UI::Theme::Tokens& tokens,
                              int index, const Layout::Rect& card) {
    const BinItem& item = items_[static_cast<std::size_t>(index)];
    const float thumb_h = card.height - kMetaHeight;
    const Layout::Rect thumb{card.x, card.y, card.width, thumb_h};
    const bool selected = selected_.count(index) != 0;
    const bool hovered = hovered_ == index;

    // Thumb: async BGRA poster -> disk PNG -> kind placeholder.
    bool drew_media = false;
    if (item.has_video) {
        if (const video::Frame* frame =
                PosterFor(item, PosterSeconds(index));
            frame != nullptr) {
            drew_media = renderer.DrawFrameBgra(
                frame->Data(), frame->Width(), frame->Height(), thumb,
                1.0f);
        }
        if (!drew_media && !item.poster_path.empty()) {
            drew_media = renderer.DrawImage(item.poster_path, thumb, 1.0f,
                                            Theme::kRadiusMd);
        }
    }
    if (!drew_media) {
        renderer.FillRect(thumb, tokens.input, Theme::kRadiusMd);
        if (!item.has_video) {
            // Audio tile: decoded peaks strip when the seam answers,
            // the classic 5-bar glyph while pending/failed.
            bool drew_wave = false;
            if (const AudioPeaks* peaks = PeaksFor(item);
                peaks != nullptr && peaks->buckets > 0) {
                const Layout::Rect strip{thumb.x + 6.0f, thumb.y + 6.0f,
                                         thumb.width - 12.0f,
                                         thumb.height - 12.0f};
                const std::size_t buckets = peaks->buckets;
                const float step = strip.width /
                                   static_cast<float>(buckets);
                const float mid = strip.y + strip.height * 0.5f;
                for (std::size_t i = 0; i < buckets; ++i) {
                    const float mn = peaks->data[i * 2];
                    const float mx = peaks->data[i * 2 + 1];
                    const float h = std::clamp(
                        (mx - mn) * 0.5f * strip.height, 1.0f,
                        strip.height);
                    renderer.FillRect(
                        {strip.x + static_cast<float>(i) * step,
                         mid - h * 0.5f,
                         std::max(1.0f, step - 1.0f), h},
                        Theme::kWaveform, 0.0f);
                }
                drew_wave = true;
            }
            if (!drew_wave) {
                const float bars[] = {3.0f, 7.0f, 11.0f, 7.0f, 3.0f};
                const float cx = thumb.x + thumb.width * 0.5f;
                const float cy = thumb.y + thumb.height * 0.5f;
                float bx = cx - 8.0f;
                for (const float bh : bars) {
                    renderer.FillRect({bx, cy - bh * 0.5f, 2.0f, bh},
                                      tokens.muted, 1.0f);
                    bx += 4.0f;
                }
            }
        }
    }

    // Kind badge — top-left chip: V / I / A (film / image / audio).
    const char* kind_text =
        !item.has_video ? "A" : (item.is_still ? "I" : "V");
    const Color kind_color = !item.has_video   ? Theme::kClipAudio
                             : item.is_still   ? Theme::kClipGenerated
                                               : Theme::kClipVideo;
    const Layout::Rect kind_chip{thumb.x + 4.0f, thumb.y + 4.0f,
                                 15.0f, 15.0f};
    renderer.FillRect(kind_chip, Theme::kOverlayChip,
                      Theme::kRadiusSm);
    renderer.DrawText(kind_text,
                      {kind_chip.x, kind_chip.y + 1.0f, kind_chip.width,
                       kind_chip.height},
                      {kind_color, Theme::kFontCaption, 700});

    // Proxy badge — states mirror ProxyStatus: pending amber "PX..",
    // ready accent "PX", failed danger "PX!".
    if (item.proxy_state != 0) {
        const Color chip_color =
            item.proxy_state == 2   ? tokens.accent
            : item.proxy_state == 3 ? tokens.danger
                                    : Theme::kWarningChip;
        const char* px_text = item.proxy_state == 1   ? "PX.."
                              : item.proxy_state == 3 ? "PX!"
                                                      : "PX";
        const Layout::Rect px_chip{
            thumb.x + 4.0f + 19.0f, thumb.y + 4.0f, 24.0f, 15.0f};
        renderer.FillRect(px_chip, Theme::kOverlayChip,
                          Theme::kRadiusSm);
        renderer.DrawText(px_text,
                          {px_chip.x + 2.0f, px_chip.y + 1.0f,
                           px_chip.width, px_chip.height},
                          {chip_color, Theme::kFontCaption, 700});
    }

    // Audio tag — bottom-right speaker chip whenever the media carries
    // an audio stream (a V file with embedded audio shows it too —
    // that's the linked-pair hint the old bin surfaced).
    if (item.has_audio) {
        const Layout::Rect au_chip{thumb.x + thumb.width - 19.0f,
                                   thumb.y + thumb.height - 19.0f,
                                   15.0f, 15.0f};
        renderer.FillRect(au_chip, Theme::kOverlayChip,
                          Theme::kRadiusSm);
        // Speaker glyph: wedge + single wave bar.
        const float sx = au_chip.x + 3.0f;
        const float sy = au_chip.y + au_chip.height * 0.5f;
        renderer.FillRect({sx, sy - 2.0f, 3.0f, 4.0f}, tokens.foreground,
                          0.5f);
        renderer.FillRect({sx + 4.5f, sy - 3.5f, 1.5f, 7.0f},
                          Theme::kClipAudio, 0.5f);
        renderer.FillRect({sx + 7.0f, sy - 2.0f, 1.5f, 4.0f},
                          Theme::kClipAudio, 0.5f);
    }

    // Duration chip — bottom-left of the thumb (translucent, muted).
    if (item.has_video || item.has_audio) {
        const std::string dur = FormatCardDuration(item.duration_seconds);
        const Layout::Rect d_chip{thumb.x + 4.0f,
                                  thumb.y + thumb.height - 19.0f,
                                  34.0f, 15.0f};
        renderer.FillRect(d_chip, Theme::kOverlayChip, Theme::kRadiusSm);
        renderer.DrawText(dur,
                          {d_chip.x + 2.0f, d_chip.y + 1.0f,
                           d_chip.width, d_chip.height},
                          {tokens.foreground, Theme::kFontCaption, 500});
    }

    // Meta strip — name only. DrawText wraps, it does not clip, so a
    // name wider than the card must be ellipsized before the draw or
    // it wraps mid-word over the next card.
    const Layout::Rect meta{card.x + 1.0f,
                            card.y + thumb_h + 3.0f, card.width - 2.0f,
                            kMetaHeight - 4.0f};
    const Rendering::TextStyle meta_style{tokens.foreground,
                                          Theme::kFontCaption, 400};
    renderer.DrawText(
        XER::UI::Widgets::EllipsizeText(
            renderer,
            item.name.empty() ? std::string_view(item.path)
                              : std::string_view(item.name),
            meta_style, meta.width),
        meta, meta_style);

    // Selection / hover chrome — drawn last so badges stay readable.
    if (hovered && !selected) {
        renderer.FillRect(card, Theme::WithAlpha(tokens.hover, 0.5f),
                          Theme::kRadiusMd);
    }
    if (selected) {
        renderer.FillRect(card, Theme::WithAlpha(tokens.accent, 0.14f),
                          Theme::kRadiusMd);
        renderer.StrokeRect(card, tokens.accent, 1.5f, Theme::kRadiusMd);
    }
}

Layout::Rect BinGridSurface::ScrollbarThumbRect() const {
    const Layout::Rect b = GetBounds();
    const float content = GetContentHeight();
    const float track_h = b.height - kBarPad * 2.0f;
    const float visible =
        content > 0.0f ? std::min(1.0f, b.height / content) : 1.0f;
    const float thumb_h = std::max(18.0f, track_h * visible);
    const float range = track_h - thumb_h;
    const float max_scroll = MaxScrollY();
    const float t = max_scroll > 0.0f ? scroll_ / max_scroll : 0.0f;
    return {b.x + b.width - kBarPad - kBarWidth,
            b.y + kBarPad + t * range, kBarWidth, thumb_h};
}

bool BinGridSurface::ScrollbarZoneContains(Layout::Point point) const {
    if (MaxScrollY() <= 0.0f) return false;
    const Layout::Rect b = GetBounds();
    return point.x >= b.x + b.width - kHitWidth && point.x < b.x + b.width;
}

void BinGridSurface::ScrollFromPointer(float y) {
    const Layout::Rect b = GetBounds();
    const float track_h = b.height - kBarPad * 2.0f;
    const float content = GetContentHeight();
    const float visible =
        content > 0.0f ? std::min(1.0f, b.height / content) : 1.0f;
    const float thumb_h = std::max(18.0f, track_h * visible);
    const float range = std::max(1.0f, track_h - thumb_h);
    const float t = (y - b.y - kBarPad - scrollbar_grab_) / range;
    ScrollTo(std::clamp(t, 0.0f, 1.0f) * MaxScrollY());
}

void BinGridSurface::DrawScrollbar(Rendering::Renderer& renderer,
                                   const XER::UI::Theme::Tokens& tokens) {
    if (MaxScrollY() <= 0.0f) return;
    const Layout::Rect thumb = ScrollbarThumbRect();
    renderer.FillRect(thumb,
                      Theme::WithAlpha(tokens.muted,
                                       scrollbar_drag_ ? 0.85f : 0.5f),
                      kBarWidth * 0.5f);
}

void BinGridSurface::Render(Rendering::Renderer& renderer,
                            const XER::UI::Theme::Tokens& tokens) {
    const Layout::Rect b = GetBounds();
    renderer.FillRect(b, tokens.panel, 0.0f);
    renderer.PushClip(b);

    const auto [first, last] = VisibleIndexRange();
    for (int i = first; i < last; ++i) {
        const Layout::Rect card = CardRect(i);
        if (card.y + card.height < b.y || card.y > b.y + b.height) {
            continue;   // viewport culling — the virtualization
        }
        DrawCard(renderer, tokens, i, card);
    }

    if (items_.empty()) {
        renderer.DrawText("Import media to begin",
                          {b.x, b.y + b.height * 0.5f - 8.0f, b.width,
                           18.0f},
                          {tokens.muted, Theme::kFontBody, 400});
    }

    // Marquee rubber band — accent-tinted like the timeline's.
    if (marquee_active_) {
        const float x0 = std::min(marquee_anchor_.x, marquee_end_.x);
        const float y0 = std::min(marquee_anchor_.y, marquee_end_.y);
        const Layout::Rect band{
            x0, y0, std::abs(marquee_end_.x - marquee_anchor_.x),
            std::abs(marquee_end_.y - marquee_anchor_.y)};
        renderer.FillRect(band, Theme::WithAlpha(tokens.accent, 0.12f),
                          0.0f);
        renderer.StrokeRect(band, Theme::WithAlpha(tokens.accent, 0.7f),
                            1.0f, 0.0f);
    }

    renderer.PopClip();
    DrawScrollbar(renderer, tokens);
}

void BinGridSurface::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    scroll_ = std::clamp(scroll_, 0.0f, MaxScrollY());
}

// ---- events -----------------------------------------------------------

void BinGridSurface::ApplyMarqueeSelection() {
    const float x0 = std::min(marquee_anchor_.x, marquee_end_.x);
    const float y0 = std::min(marquee_anchor_.y, marquee_end_.y);
    const Layout::Rect band{
        x0, y0, std::abs(marquee_end_.x - marquee_anchor_.x),
        std::abs(marquee_end_.y - marquee_anchor_.y)};
    std::set<int> hit;
    for (int i = 0; i < ItemCount(); ++i) {
        const Layout::Rect card = CardRect(i);
        const bool overlap =
            card.x < band.x + band.width &&
            card.x + card.width > band.x &&
            card.y < band.y + band.height &&
            card.y + card.height > band.y;
        if (overlap) hit.insert(i);
    }
    std::set<int> next = marquee_base_;
    next.insert(hit.begin(), hit.end());
    if (next != selected_) {
        selected_ = std::move(next);
        if (on_selection_changed_) on_selection_changed_();
    }
    Invalidate();
}

std::vector<std::string> BinGridSurface::DragPaths() const {
    // The pressed card leads; when it was already part of the
    // selection the whole selection travels (Premiere bin behavior).
    std::vector<std::string> out;
    if (pressed_index_ < 0) return out;
    if (selected_.count(pressed_index_)) {
        out = SelectedPaths();
    } else if (const BinItem* item = Item(pressed_index_)) {
        out.push_back(item->path);
    }
    return out;
}

void BinGridSurface::CancelGesture() {
    pressed_index_ = -1;
    pressed_was_selected_ = false;
    drag_armed_ = false;
    marquee_active_ = false;
    scrollbar_drag_ = false;
    if (drag_active_) {
        drag_active_ = false;
        if (on_end_drag_) on_end_drag_({});
    }
}

bool BinGridSurface::HandleEvent(const XER::UI::Widgets::Event& event) {
    const auto& p = event.position;

    switch (event.type) {
        case XER::UI::Widgets::Event::Type::MouseMove: {
            if (scrollbar_drag_) {
                ScrollFromPointer(p.y);
                return true;
            }
            if (drag_armed_ && !drag_active_) {
                const float dx = p.x - press_point_.x;
                const float dy = p.y - press_point_.y;
                if (std::abs(dx) + std::abs(dy) > kDragThreshold) {
                    drag_active_ = true;
                    if (on_begin_drag_) {
                        on_begin_drag_(DragPaths(), p);
                    }
                }
            }
            if (drag_active_) {
                if (on_drag_update_) on_drag_update_(p);
                return true;   // capture keeps moves routed here
            }
            if (marquee_active_) {
                marquee_end_ = p;
                ApplyMarqueeSelection();
                return true;
            }
            const int hit =
                GetBounds().Contains(p) ? ItemAtPoint(p) : -1;
            // Hover-scrub bucket: pointer x inside the card maps to a
            // media-time slice (PosterSeconds reads it at paint).
            int bucket = -1;
            if (hit >= 0) {
                const Layout::Rect card = CardRect(hit);
                if (card.width > 0.0f) {
                    const float norm =
                        std::clamp((p.x - card.x) / card.width, 0.0f,
                                   0.999f);
                    bucket =
                        static_cast<int>(norm * kScrubBuckets);
                }
            }
            if (hit != hovered_ || bucket != hover_bucket_) {
                hovered_ = hit;
                hover_bucket_ = bucket;
                Invalidate();
            }
            return GetBounds().Contains(p) || pressed_index_ >= 0;
        }
        case XER::UI::Widgets::Event::Type::MouseDown: {
            if (event.mouse_button ==
                XER::UI::Widgets::MouseButton::Left) {
                if (ScrollbarZoneContains(p)) {
                    const auto thumb = ScrollbarThumbRect();
                    scrollbar_grab_ = p.y - thumb.y;
                    if (scrollbar_grab_ < 0.0f ||
                        scrollbar_grab_ > thumb.height) {
                        // Track click pages toward the pointer.
                        const float mid = thumb.y + thumb.height * 0.5f;
                        ScrollTo(scroll_ +
                                 (p.y < mid ? -GetBounds().height * 0.8f
                                            : GetBounds().height * 0.8f));
                        scrollbar_grab_ =
                            ScrollbarThumbRect().height * 0.5f;
                    }
                    scrollbar_drag_ = true;
                    return true;
                }
                const int hit = ItemAtPoint(p);
                pressed_index_ = hit;
                pressed_was_selected_ =
                    hit >= 0 && selected_.count(hit) != 0;
                press_point_ = p;
                if (hit >= 0) {
                    drag_armed_ = true;
                    // Press-on-unselected applies selection NOW so a
                    // straight drag grabs the right payload (Premiere
                    // bin behavior); a selected card keeps the group
                    // until MouseUp decides click-vs-drag.
                    if (!selected_.count(hit)) {
                        if (event.is_ctrl) {
                            selected_.insert(hit);
                        } else if (event.is_shift && anchor_ >= 0) {
                            const int lo = std::min(anchor_, hit);
                            const int hi = std::max(anchor_, hit);
                            selected_.clear();
                            for (int i = lo; i <= hi; ++i) {
                                selected_.insert(i);
                            }
                        } else {
                            selected_ = {hit};
                            anchor_ = hit;
                        }
                        if (on_selection_changed_) {
                            on_selection_changed_();
                        }
                        Invalidate();
                    }
                    return true;
                }
                // Empty space: left-drag begins a marquee (ctrl adds).
                marquee_active_ = true;
                marquee_anchor_ = p;
                marquee_end_ = p;
                marquee_base_ = event.is_ctrl ? selected_
                                              : std::set<int>{};
                if (!event.is_ctrl && !selected_.empty()) {
                    selected_.clear();
                    if (on_selection_changed_) on_selection_changed_();
                }
                return true;
            }
            if (event.mouse_button ==
                XER::UI::Widgets::MouseButton::Right) {
                const int hit = ItemAtPoint(p);
                // Right-click on an unselected card selects it alone —
                // the menu actions then act on the clicked item.
                if (hit >= 0 && !selected_.count(hit)) {
                    selected_ = {hit};
                    anchor_ = hit;
                    if (on_selection_changed_) on_selection_changed_();
                    Invalidate();
                }
                if (on_context_menu_) on_context_menu_(hit, p);
                return true;
            }
            return false;
        }
        case XER::UI::Widgets::Event::Type::MouseUp: {
            if (event.mouse_button !=
                XER::UI::Widgets::MouseButton::Left) {
                return false;
            }
            if (scrollbar_drag_) {
                scrollbar_drag_ = false;
                return true;
            }
            const bool was_dragging = drag_active_;
            const int pressed = pressed_index_;
            drag_active_ = false;
            drag_armed_ = false;
            pressed_index_ = -1;
            if (was_dragging) {
                if (on_end_drag_) on_end_drag_(p);
                return true;
            }
            if (marquee_active_) {
                marquee_active_ = false;
                ApplyMarqueeSelection();
                Invalidate();
                return true;
            }
            // A release on a selected card that did NOT move resolves
            // click-vs-drag: collapse the group to the clicked card
            // (unless modifiers asked for toggle/range behavior).
            const int hit = ItemAtPoint(p);
            if (pressed >= 0 && hit == pressed &&
                selected_.count(pressed) && selected_.size() > 1 &&
                !event.is_ctrl && !event.is_shift) {
                selected_ = {pressed};
                anchor_ = pressed;
                if (on_selection_changed_) on_selection_changed_();
                Invalidate();
            }
            // Ctrl+release toggles off only a card that was selected
            // BEFORE the press (the press itself adds new picks).
            if (pressed >= 0 && hit == pressed && event.is_ctrl &&
                pressed_was_selected_ && selected_.count(pressed)) {
                selected_.erase(pressed);
                if (on_selection_changed_) on_selection_changed_();
                Invalidate();
            }
            // Double-click synthesis: same card released twice inside
            // the window+radius activates it. Modifier releases are
            // selection gestures, not activation gestures — they neither
            // evaluate nor reset the bookkeeping, so
            // click / ctrl+click / click still reads as a double-click.
            if (!event.is_ctrl && !event.is_shift) {
                const auto now = std::chrono::steady_clock::now();
                const bool dbl =
                    hit >= 0 && hit == last_up_index_ &&
                    now - last_up_time_ <= kDoubleClickWindow &&
                    std::abs(p.x - last_up_point_.x) <=
                        kDoubleClickRadius &&
                    std::abs(p.y - last_up_point_.y) <=
                        kDoubleClickRadius;
                last_up_index_ = hit;
                last_up_time_ = now;
                last_up_point_ = p;
                if (dbl && on_activate_item_) {
                    on_activate_item_(hit);
                }
            }
            return hit >= 0;
        }
        case XER::UI::Widgets::Event::Type::MouseWheel: {
            // Three cards per notch — the VirtualList convention.
            const Layout::Size size = CardSize();
            ScrollTo(scroll_ - static_cast<float>(event.wheel_delta) /
                                   120.0f * (size.height + kGap) * 3.0f);
            return true;
        }
        case XER::UI::Widgets::Event::Type::KeyDown: {
            switch (event.key) {
                case XER::UI::Widgets::KeyCode::A:
                    if (event.is_ctrl) {
                        std::set<int> all;
                        for (int i = 0; i < ItemCount(); ++i) {
                            all.insert(i);
                        }
                        if (all != selected_) {
                            selected_ = std::move(all);
                            if (on_selection_changed_) {
                                on_selection_changed_();
                            }
                            Invalidate();
                        }
                        return true;
                    }
                    return false;
                case XER::UI::Widgets::KeyCode::Escape:
                    if (!selected_.empty()) {
                        selected_.clear();
                        if (on_selection_changed_) {
                            on_selection_changed_();
                        }
                        Invalidate();
                    }
                    CancelGesture();
                    return true;
                default:
                    return false;
            }
        }
        case XER::UI::Widgets::Event::Type::Blur:
            CancelGesture();
            return false;
        default:
            return false;
    }
}

}  // namespace XER::VideoEditor::Ui2
