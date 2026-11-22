// tile_store.cc — see tile_store.h for the KisTiledDataManager-derived
// semantics this implements.
#include "photo/tile_store.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::Photo {

namespace {

// Writes one pixel in device format: RGBA8 stores the quad, Gray8/A8
// stores the coverage byte (p.a — the "A" in A8).
inline void StorePixelBytes(uint8_t* dst, int bpp, Pixel p) {
    if (bpp >= 4) {
        dst[0] = p.r;
        dst[1] = p.g;
        dst[2] = p.b;
        dst[3] = p.a;
    } else {
        dst[0] = p.a;
    }
}

}  // namespace

TiledDevice::TiledDevice(int width, int height, image::PixelFormat format,
                         Pixel default_pixel)
    : width_(width < 0 ? 0 : width),
      height_(height < 0 ? 0 : height),
      format_(format),
      default_pixel_(default_pixel) {}

std::unique_ptr<TiledDevice> TiledDevice::Clone() const {
    auto out = std::make_unique<TiledDevice>(width_, height_, format_,
                                             default_pixel_);
    // Share every tile — writes on either side go through TileForWrite
    // which clones when use_count() > 1.
    out->tiles_ = tiles_;
    out->default_tile_ = default_tile_;
    return out;
}

int TiledDevice::BytesPerPixel() const {
    const int bpp = image::BytesPerPixel(format_);
    return bpp > 0 ? bpp : 4;
}

void TiledDevice::SetDefaultPixel(Pixel p) {
    default_pixel_ = p;
    default_tile_.reset();  // rebuilt lazily
}

const std::shared_ptr<Tile>& TiledDevice::DefaultTile() {
    if (!default_tile_) {
        auto t = std::make_shared<Tile>();
        const int bpp = BytesPerPixel();
        t->data.assign(TileBytes(), 0);
        for (size_t i = 0; i < TileBytes();
             i += static_cast<size_t>(bpp)) {
            StorePixelBytes(t->data.data() + i, bpp, default_pixel_);
        }
        default_tile_ = std::move(t);
    }
    return default_tile_;
}

const Tile* TiledDevice::TileAt(int col, int row) const {
    const auto it = tiles_.find(KeyOf(col, row));
    return it == tiles_.end() ? nullptr : it->second.get();
}

Tile* TiledDevice::TileForWrite(int col, int row) {
    const int64_t key = KeyOf(col, row);
    auto it = tiles_.find(key);
    if (it == tiles_.end()) {
        // New tile: starts as the shared default tile so partial writes
        // read the default pixel around the stroke.
        if (active_memento_) {
            active_memento_->old_tiles_.emplace(key, nullptr);
        }
        auto& slot = tiles_[key];
        slot = DefaultTile();  // shared; cloned below on first touch
        // The shared default always has use_count() > 1, so this falls
        // through to the clone path — unless a transaction already
        // recorded this tile (impossible: it did not exist).
    }
    auto& slot = tiles_[key];
    if (active_memento_ &&
        active_memento_->old_tiles_.find(key) ==
            active_memento_->old_tiles_.end()) {
        // First touch inside the transaction: keep the old tile
        // immutable for rollback, always swap in a private copy.
        active_memento_->old_tiles_[key] = slot;
        slot = std::make_shared<Tile>(*slot);
    } else if (slot.use_count() > 1) {
        // Shared via Clone()/default tile outside a transaction:
        // copy-on-write.
        slot = std::make_shared<Tile>(*slot);
    }
    return slot.get();
}

Pixel TiledDevice::ReadPixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return default_pixel_;
    }
    const Tile* t = TileAt(x / kTileSize, y / kTileSize);
    if (t == nullptr) return default_pixel_;
    const int bpp = BytesPerPixel();
    const size_t off =
        (static_cast<size_t>(y % kTileSize) * kTileSize +
         static_cast<size_t>(x % kTileSize)) *
        static_cast<size_t>(bpp);
    if (bpp >= 4) {
        return Pixel{t->data[off], t->data[off + 1], t->data[off + 2],
                     t->data[off + 3]};
    }
    // A8: the stored byte is the coverage — expose it as alpha.
    return Pixel{0, 0, 0, t->data[off]};
}

void TiledDevice::WritePixel(int x, int y, Pixel p) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    Tile* t = TileForWrite(x / kTileSize, y / kTileSize);
    const int bpp = BytesPerPixel();
    const size_t off =
        (static_cast<size_t>(y % kTileSize) * kTileSize +
         static_cast<size_t>(x % kTileSize)) *
        static_cast<size_t>(bpp);
    StorePixelBytes(t->data.data() + off, bpp, p);
}

void TiledDevice::Fill(const IntRect& rect, Pixel p) {
    const IntRect r = IntRect::Intersect(rect, Bounds());
    if (r.IsEmpty()) return;

    const int col0 = r.x / kTileSize;
    const int col1 = (r.Right() - 1) / kTileSize;
    const int row0 = r.y / kTileSize;
    const int row1 = (r.Bottom() - 1) / kTileSize;

    for (int row = row0; row <= row1; ++row) {
        for (int col = col0; col <= col1; ++col) {
            const IntRect tile_rect{col * kTileSize, row * kTileSize,
                                    kTileSize, kTileSize};
            const IntRect sub = IntRect::Intersect(r, tile_rect);
            if (sub == tile_rect && p == default_pixel_) {
                // Whole tile becomes default: keep it sparse by sharing
                // the canonical default tile (COW makes it free).
                const int64_t key = KeyOf(col, row);
                auto it = tiles_.find(key);
                if (it != tiles_.end()) {
                    if (active_memento_ &&
                        active_memento_->old_tiles_.find(key) ==
                            active_memento_->old_tiles_.end()) {
                        active_memento_->old_tiles_[key] = it->second;
                    }
                    it->second = DefaultTile();
                }
                continue;
            }
            if (sub == tile_rect) {
                // Whole-tile uniform fill — fast path.
                Tile* t = TileForWrite(col, row);
                const int bpp = BytesPerPixel();
                for (size_t i = 0; i < TileBytes();
                     i += static_cast<size_t>(bpp)) {
                    StorePixelBytes(t->data.data() + i, bpp, p);
                }
                continue;
            }
            for (int y = sub.y; y < sub.Bottom(); ++y) {
                for (int x = sub.x; x < sub.Right(); ++x) {
                    WritePixel(x, y, p);
                }
            }
        }
    }
}

void TiledDevice::Clear() {
    tiles_.clear();
    default_tile_.reset();
}

IntRect TiledDevice::Extent() const {
    IntRect out;
    for (const auto& [key, tile] : tiles_) {
        const int64_t col = key >> 32;
        const int64_t row = static_cast<int64_t>(
            static_cast<int32_t>(key & 0xFFFFFFFFLL));
        out = IntRect::Union(
            out, IntRect{static_cast<int>(col * kTileSize),
                         static_cast<int>(row * kTileSize), kTileSize,
                         kTileSize});
    }
    return IntRect::Intersect(out, Bounds());
}

std::vector<uint8_t> TiledDevice::ReadRegion(const IntRect& rect) const {
    const IntRect r = IntRect::Intersect(rect, Bounds());
    if (r.IsEmpty()) return {};
    std::vector<uint8_t> out(static_cast<size_t>(r.w) * r.h * 4);
    const int bpp = BytesPerPixel();
    const uint8_t def[4] = {default_pixel_.r, default_pixel_.g,
                            default_pixel_.b, default_pixel_.a};
    for (int y = 0; y < r.h; ++y) {
        uint8_t* dst = out.data() + static_cast<size_t>(y) * r.w * 4;
        const int dy = r.y + y;
        const int row_ = dy / kTileSize;
        const int in_y = dy % kTileSize;
        for (int x = 0; x < r.w; ++x) {
            const int dx = r.x + x;
            const Tile* t = TileAt(dx / kTileSize, row_);
            if (t == nullptr) {
                std::memcpy(dst, def, 4);
            } else {
                const size_t off =
                    (static_cast<size_t>(in_y) * kTileSize +
                     static_cast<size_t>(dx % kTileSize)) *
                    static_cast<size_t>(bpp);
                if (bpp >= 4) {
                    std::memcpy(dst, t->data.data() + off, 4);
                } else {
                    // A8 -> normalized {0,0,0,coverage}.
                    dst[0] = dst[1] = dst[2] = 0;
                    dst[3] = t->data[off];
                }
            }
            dst += 4;
        }
    }
    return out;
}

void TiledDevice::WriteRegion(const IntRect& rect, const uint8_t* src,
                              size_t src_stride_bytes) {
    if (src == nullptr) return;
    const IntRect r = IntRect::Intersect(rect, Bounds());
    if (r.IsEmpty()) return;
    const size_t stride =
        src_stride_bytes ? src_stride_bytes
                         : static_cast<size_t>(r.w) * 4;
    // Source rows correspond to rect.y..; skipped clipped top rows are
    // accounted by offsetting into src.
    const int src_x0 = r.x - rect.x;
    const int src_y0 = r.y - rect.y;
    for (int y = 0; y < r.h; ++y) {
        const uint8_t* s =
            src + (static_cast<size_t>(src_y0 + y) * stride) +
            static_cast<size_t>(src_x0) * 4;
        const int dy = r.y + y;
        for (int x = 0; x < r.w; ++x, s += 4) {
            WritePixel(r.x + x, dy, Pixel{s[0], s[1], s[2], s[3]});
        }
    }
}

std::vector<uint32_t> TiledDevice::ReadRegionU32(const IntRect& rect) const {
    const std::vector<uint8_t> bytes = ReadRegion(rect);
    const IntRect r = IntRect::Intersect(rect, Bounds());
    std::vector<uint32_t> out(bytes.size() / 4);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = Pixel{bytes[i * 4], bytes[i * 4 + 1], bytes[i * 4 + 2],
                       bytes[i * 4 + 3]}
                     .PackedU32();
    }
    (void)r;
    return out;
}

void TiledDevice::WriteRegionU32(const IntRect& rect,
                                 const std::vector<uint32_t>& src) {
    const IntRect r = IntRect::Intersect(rect, Bounds());
    if (r.IsEmpty()) return;
    const int src_x0 = r.x - rect.x;
    const int src_y0 = r.y - rect.y;
    const int src_w = rect.w;  // packed row width of `src`
    for (int y = 0; y < r.h; ++y) {
        for (int x = 0; x < r.w; ++x) {
            const size_t si = static_cast<size_t>(src_y0 + y) * src_w +
                              static_cast<size_t>(src_x0 + x);
            if (si >= src.size()) return;
            WritePixel(r.x + x, r.y + y, Pixel::FromU32(src[si]));
        }
    }
}

bool TiledDevice::CompositeRect(const IntRect& clip,
                                const std::vector<uint32_t>& src, int src_w,
                                int src_h, int dx, int dy,
                                const std::string& blend_mode,
                                float opacity) {
    if (src_w <= 0 || src_h <= 0) return false;
    if (static_cast<size_t>(src_w) * src_h > src.size()) return false;
    // Where the src image lands on the device, clipped by `clip` and
    // the device bounds.
    const IntRect placed{dx, dy, src_w, src_h};
    const IntRect eff =
        IntRect::Intersect(IntRect::Intersect(placed, clip), Bounds());
    if (eff.IsEmpty()) return false;

    // Sub-rect of src covering eff.
    const int sx0 = eff.x - dx;
    const int sy0 = eff.y - dy;
    std::vector<uint32_t> sub(static_cast<size_t>(eff.w) * eff.h);
    for (int y = 0; y < eff.h; ++y) {
        std::copy_n(src.data() + static_cast<size_t>(sy0 + y) * src_w + sx0,
                    eff.w, sub.data() + static_cast<size_t>(y) * eff.w);
    }
    std::vector<uint32_t> dst = ReadRegionU32(eff);
    if (!XER::Bridge::Skia::ImageComposite(&dst, eff.w, eff.h, sub, eff.w,
                                           eff.h, 0, 0, blend_mode,
                                           opacity)) {
        return false;
    }
    WriteRegionU32(eff, dst);
    return true;
}

void TiledDevice::BeginTransaction() {
    // Krita allows nested transactions via a memento stack; M1 keeps a
    // single level — history BeginMacro() is the grouping mechanism.
    assert(!active_memento_ && "TiledDevice: nested transaction");
    if (active_memento_) return;
    active_memento_ = std::make_unique<TileMemento>();
}

std::unique_ptr<TileMemento> TiledDevice::EndTransaction() {
    return std::move(active_memento_);
}

void TiledDevice::AbortTransaction() {
    if (!active_memento_) return;
    for (auto& [key, old] : active_memento_->old_tiles_) {
        if (old) {
            tiles_[key] = old;
        } else {
            tiles_.erase(key);
        }
    }
    active_memento_.reset();
}

std::unique_ptr<TileMemento> TiledDevice::ApplyMemento(
    const TileMemento& m) {
    auto inverse = std::make_unique<TileMemento>();
    inverse->old_tiles_.reserve(m.old_tiles_.size());
    for (const auto& [key, old] : m.old_tiles_) {
        const auto it = tiles_.find(key);
        inverse->old_tiles_[key] =
            (it == tiles_.end()) ? nullptr : it->second;
        if (old) {
            tiles_[key] = old;
        } else {
            tiles_.erase(key);
        }
    }
    return inverse;
}

bool TiledDevice::IdenticalPixels(const TiledDevice& other) const {
    if (width_ != other.width_ || height_ != other.height_ ||
        format_ != other.format_) {
        return false;
    }
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (ReadPixel(x, y) != other.ReadPixel(x, y)) return false;
        }
    }
    return true;
}

// --- coverage access (A8/Gray8) ---------------------------------------

uint8_t TiledDevice::ReadCoverage(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return default_pixel_.a;
    }
    const Tile* t = TileAt(x / kTileSize, y / kTileSize);
    if (t == nullptr) return default_pixel_.a;
    const int bpp = BytesPerPixel();
    const size_t off =
        (static_cast<size_t>(y % kTileSize) * kTileSize +
         static_cast<size_t>(x % kTileSize)) *
        static_cast<size_t>(bpp);
    return bpp >= 4 ? t->data[off + 3] : t->data[off];
}

void TiledDevice::WriteCoverage(int x, int y, uint8_t v) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    Tile* t = TileForWrite(x / kTileSize, y / kTileSize);
    const int bpp = BytesPerPixel();
    const size_t off =
        (static_cast<size_t>(y % kTileSize) * kTileSize +
         static_cast<size_t>(x % kTileSize)) *
        static_cast<size_t>(bpp);
    if (bpp >= 4) {
        t->data[off + 3] = v;  // RGBA8: coverage = alpha, RGB kept
    } else {
        t->data[off] = v;
    }
}

void TiledDevice::FillCoverage(const IntRect& rect, uint8_t v) {
    const IntRect r = IntRect::Intersect(rect, Bounds());
    if (r.IsEmpty()) return;
    const int bpp = BytesPerPixel();
    const int col0 = r.x / kTileSize;
    const int col1 = (r.Right() - 1) / kTileSize;
    const int row0 = r.y / kTileSize;
    const int row1 = (r.Bottom() - 1) / kTileSize;
    for (int row = row0; row <= row1; ++row) {
        for (int col = col0; col <= col1; ++col) {
            const IntRect tile_rect{col * kTileSize, row * kTileSize,
                                    kTileSize, kTileSize};
            const IntRect sub = IntRect::Intersect(r, tile_rect);
            if (bpp == 1 && sub == tile_rect &&
                v == default_pixel_.a) {
                // Whole A8 tile returns to default coverage: re-share
                // the canonical default tile (same trick as Fill()).
                const int64_t key = KeyOf(col, row);
                auto it = tiles_.find(key);
                if (it != tiles_.end()) {
                    if (active_memento_ &&
                        active_memento_->old_tiles_.find(key) ==
                            active_memento_->old_tiles_.end()) {
                        active_memento_->old_tiles_[key] = it->second;
                    }
                    it->second = DefaultTile();
                }
                continue;
            }
            Tile* t = TileForWrite(col, row);
            for (int y = sub.y; y < sub.Bottom(); ++y) {
                const int in_y = y % kTileSize;
                for (int x = sub.x; x < sub.Right(); ++x) {
                    const size_t off =
                        (static_cast<size_t>(in_y) * kTileSize +
                         static_cast<size_t>(x % kTileSize)) *
                        static_cast<size_t>(bpp);
                    if (bpp >= 4) {
                        t->data[off + 3] = v;
                    } else {
                        t->data[off] = v;
                    }
                }
            }
        }
    }
}

std::vector<uint8_t> TiledDevice::ReadRegionCoverage(
    const IntRect& rect) const {
    const IntRect r = IntRect::Intersect(rect, Bounds());
    if (r.IsEmpty()) return {};
    std::vector<uint8_t> out(static_cast<size_t>(r.w) * r.h);
    const int bpp = BytesPerPixel();
    for (int y = 0; y < r.h; ++y) {
        uint8_t* dst = out.data() + static_cast<size_t>(y) * r.w;
        const int dy = r.y + y;
        const int row_ = dy / kTileSize;
        const int in_y = dy % kTileSize;
        for (int x = 0; x < r.w; ++x) {
            const int dx = r.x + x;
            const Tile* t = TileAt(dx / kTileSize, row_);
            if (t == nullptr) {
                dst[x] = default_pixel_.a;
            } else {
                const size_t off =
                    (static_cast<size_t>(in_y) * kTileSize +
                     static_cast<size_t>(dx % kTileSize)) *
                    static_cast<size_t>(bpp);
                dst[x] = bpp >= 4 ? t->data[off + 3] : t->data[off];
            }
        }
    }
    return out;
}

void TiledDevice::WriteRegionCoverage(const IntRect& rect,
                                      const uint8_t* src,
                                      size_t src_stride_bytes) {
    if (src == nullptr) return;
    const IntRect r = IntRect::Intersect(rect, Bounds());
    if (r.IsEmpty()) return;
    const size_t stride =
        src_stride_bytes ? src_stride_bytes
                         : static_cast<size_t>(rect.w);
    const int src_x0 = r.x - rect.x;
    const int src_y0 = r.y - rect.y;
    const int bpp = BytesPerPixel();
    for (int y = 0; y < r.h; ++y) {
        const uint8_t* s = src +
            (static_cast<size_t>(src_y0 + y) * stride) +
            static_cast<size_t>(src_x0);
        const int dy = r.y + y;
        const int row_ = dy / kTileSize;
        const int in_y = dy % kTileSize;
        for (int x = 0; x < r.w; ++x) {
            const int dx = r.x + x;
            Tile* t = TileForWrite(dx / kTileSize, row_);
            const size_t off =
                (static_cast<size_t>(in_y) * kTileSize +
                 static_cast<size_t>(dx % kTileSize)) *
                static_cast<size_t>(bpp);
            if (bpp >= 4) {
                t->data[off + 3] = s[x];
            } else {
                t->data[off] = s[x];
            }
        }
    }
}

}  // namespace XER::VideoEditor::Photo
