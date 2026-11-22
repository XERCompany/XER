// tile_store.h — TiledDevice: sparse 64x64-tile raster store with
// copy-on-write tiles and transaction/memento undo, modeled on Krita's
// KisTiledDataManager / KisMementoManager semantics (see
// docs/wiki/research/2026-11-21-graphics-editor-stack.md §3).
//
// M1 notes:
//  * Storage is RGBA8 (4 B/px, straight alpha). The format travels as
//    image::PixelFormat so the store can promote to RGBA32F/RGBA_F16
//    later — nothing outside this header may assume "8-bit" (the
//    op-graph seam stays format-typed).
//  * A tile is only allocated on first write; reads over holes return
//    the device default pixel (Krita default-pixel semantics).
//  * Copy-on-write: tiles are shared_ptr-managed. Clone() shares them;
//    a write to a tile with refcount > 1 clones first. During an open
//    transaction the FIRST write to a tile always clones — the old
//    tile is kept immutable inside the memento so rollback is an
//    O(touched-tiles) pointer swap.
//  * Undo delta = TileMemento: coord -> old tile (nullptr = tile did
//    not exist). ApplyMemento() swaps old state back in and returns
//    the INVERSE memento, giving undo<->redo ping-pong for free.
//
// M4 notes (A8 coverage devices — layer masks + selections):
//  * PixelFormat::Gray8 (1 B/px) is a first-class device format — the
//    "A8" coverage store for masks/selections. Tiles are still
//    kTileSize² pixels, just 1 byte each.
//  * The Pixel/region API stays NORMALIZED to RGBA quads regardless
//    of format: on a Gray8 device ReadPixel returns {0,0,0,v},
//    WritePixel stores p.a, ReadRegion still yields w*h*4 bytes.
//    The dedicated *Coverage* accessors below are the compact
//    single-byte path used by masks, selections, and .xerphoto.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "image/core/pixel_format.h"

namespace XER::VideoEditor::Photo {

// Integer point in document pixel space (lasso vertices, clone
// sources, wand seeds).
struct IntPoint {
    int x = 0;
    int y = 0;

    bool operator==(const IntPoint& o) const {
        return x == o.x && y == o.y;
    }
    bool operator!=(const IntPoint& o) const { return !(*this == o); }
};

// Integer rect in document pixel space.
struct IntRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool IsEmpty() const { return w <= 0 || h <= 0; }
    int Right() const { return x + w; }
    int Bottom() const { return y + h; }
    bool Contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    bool operator==(const IntRect& o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
    bool operator!=(const IntRect& o) const { return !(*this == o); }

    static IntRect Intersect(const IntRect& a, const IntRect& b) {
        const int x0 = a.x > b.x ? a.x : b.x;
        const int y0 = a.y > b.y ? a.y : b.y;
        const int x1 = a.Right() < b.Right() ? a.Right() : b.Right();
        const int y1 = a.Bottom() < b.Bottom() ? a.Bottom() : b.Bottom();
        if (x1 <= x0 || y1 <= y0) return {};
        return {x0, y0, x1 - x0, y1 - y0};
    }
    static IntRect Union(const IntRect& a, const IntRect& b) {
        if (a.IsEmpty()) return b;
        if (b.IsEmpty()) return a;
        const int x0 = a.x < b.x ? a.x : b.x;
        const int y0 = a.y < b.y ? a.y : b.y;
        const int x1 = a.Right() > b.Right() ? a.Right() : b.Right();
        const int y1 = a.Bottom() > b.Bottom() ? a.Bottom() : b.Bottom();
        return {x0, y0, x1 - x0, y1 - y0};
    }
};

// One RGBA8 pixel, straight (non-premultiplied) alpha.
struct Pixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;

    static Pixel RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r, g, b, a};
    }
    static Pixel Transparent() { return {}; }

    bool operator==(const Pixel& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const Pixel& o) const { return !(*this == o); }

    // Bridge/Skia packed word (R<<24 | G<<16 | B<<8 | A) used by
    // XER::Bridge::Skia flat APIs.
    uint32_t PackedU32() const {
        return (static_cast<uint32_t>(r) << 24) |
               (static_cast<uint32_t>(g) << 16) |
               (static_cast<uint32_t>(b) << 8) |
               static_cast<uint32_t>(a);
    }
    static Pixel FromU32(uint32_t v) {
        return {static_cast<uint8_t>((v >> 24) & 0xFF),
                static_cast<uint8_t>((v >> 16) & 0xFF),
                static_cast<uint8_t>((v >> 8) & 0xFF),
                static_cast<uint8_t>(v & 0xFF)};
    }
};

// Per-channel straight-alpha lerp a->b at coverage cov (0..255).
// Shared by selection-clipped fills, mask blending, and adjustment
// write-back so the rounding convention stays identical everywhere.
inline Pixel LerpPixelCov(Pixel a, Pixel b, uint8_t cov) {
    const int c = cov;
    const int inv = 255 - c;
    return {static_cast<uint8_t>((a.r * inv + b.r * c + 127) / 255),
            static_cast<uint8_t>((a.g * inv + b.g * c + 127) / 255),
            static_cast<uint8_t>((a.b * inv + b.b * c + 127) / 255),
            static_cast<uint8_t>((a.a * inv + b.a * c + 127) / 255)};
}

// Same on bridge-packed u32 words.
inline uint32_t LerpU32Cov(uint32_t a, uint32_t b, uint8_t cov) {
    return LerpPixelCov(Pixel::FromU32(a), Pixel::FromU32(b), cov)
        .PackedU32();
}

// Scales the alpha byte of a packed u32 by cov/255 (rounded) — the
// straight-storage equivalent of premultiplied mask multiply.
inline uint32_t ScaleU32Alpha(uint32_t v, uint8_t cov) {
    const uint32_t a = v & 0xFFu;
    return (v & ~0xFFu) | ((a * cov + 127u) / 255u);
}

// A single 64x64 tile. Kept behind shared_ptr for copy-on-write; the
// buffer is always kTileSize*kTileSize*bpp bytes of packed pixels in
// the device format (RGBA8 at M1).
struct Tile {
    std::vector<uint8_t> data;
};

// Opaque undo delta produced by one transaction. Holds the OLD tile
// pointer per touched coord (nullptr value = tile did not exist before
// the transaction). Restoring is a pointer swap — no pixel copying on
// the undo path itself.
class TileMemento {
public:
    TileMemento() = default;
    // Number of tiles whose pre-transaction state is recorded.
    size_t SavedTileCount() const { return old_tiles_.size(); }
    bool Empty() const { return old_tiles_.empty(); }

private:
    friend class TiledDevice;
    std::unordered_map<int64_t, std::shared_ptr<Tile>> old_tiles_;
};

class TiledDevice {
public:
    static constexpr int kTileSize = 64;

    // format: RGBA8 (4 B/px) and Gray8/A8 (1 B/px) are supported. On a
    // Gray8 device the byte IS the pixel — the Pixel-level API maps it
    // onto the alpha channel ({0,0,0,v} reads, p.a writes), and the
    // *Coverage accessors below do the same without the RGBA
    // normalization.
    TiledDevice(int width, int height,
                image::PixelFormat format = image::PixelFormat::RGBA8,
                Pixel default_pixel = Pixel::Transparent());
    ~TiledDevice() = default;

    TiledDevice(const TiledDevice&) = delete;
    TiledDevice& operator=(const TiledDevice&) = delete;

    // COW clone: shares all tiles with the source; the first write on
    // either side clones the touched tile. (Krita uses the same trick
    // for layer snapshots.)
    std::unique_ptr<TiledDevice> Clone() const;

    int Width() const { return width_; }
    int Height() const { return height_; }
    IntRect Bounds() const { return {0, 0, width_, height_}; }
    image::PixelFormat Format() const { return format_; }
    int BytesPerPixel() const;

    Pixel DefaultPixel() const { return default_pixel_; }
    void SetDefaultPixel(Pixel p);

    // Bounding rect of allocated tiles, clipped to Bounds(). Empty when
    // nothing has been painted.
    IntRect Extent() const;

    Pixel ReadPixel(int x, int y) const;
    void WritePixel(int x, int y, Pixel p);
    void Fill(const IntRect& rect, Pixel p);
    // Reset every tile to the default pixel (drops the whole map; not
    // transaction-recorded — call inside a transaction if undo needed).
    void Clear();

    // Region I/O on packed rows. ReadRegion returns rect.w*rect.h*4
    // bytes of R,G,B,A regardless of device format (a Gray8 device
    // expands to {0,0,0,v}); holes read back as the default pixel.
    std::vector<uint8_t> ReadRegion(const IntRect& rect) const;
    // src_stride_bytes: bytes per source row (0 = tightly packed).
    // Source is RGBA quads on any format — a Gray8 device stores the
    // alpha byte only.
    void WriteRegion(const IntRect& rect, const uint8_t* src,
                     size_t src_stride_bytes = 0);

    // Same, in Skia-bridge packed u32 (R<<24|G<<16|B<<8|A).
    std::vector<uint32_t> ReadRegionU32(const IntRect& rect) const;
    void WriteRegionU32(const IntRect& rect,
                        const std::vector<uint32_t>& src);

    // --- coverage access (A8/Gray8 masks + selections) --------------
    // Single-byte coverage I/O: on a Gray8 device this is the pixel
    // itself; on an RGBA8 device it is the alpha channel (RGB is left
    // untouched by writes). All of these ride the same COW/transaction
    // machinery as the RGBA paths.
    uint8_t ReadCoverage(int x, int y) const;
    void WriteCoverage(int x, int y, uint8_t v);
    void FillCoverage(const IntRect& rect, uint8_t v);
    // Compact region I/O: rect.w*rect.h coverage bytes (1 B/px), the
    // wire format .xerphoto uses for mask extents.
    std::vector<uint8_t> ReadRegionCoverage(const IntRect& rect) const;
    void WriteRegionCoverage(const IntRect& rect, const uint8_t* src,
                             size_t src_stride_bytes = 0);

    // Composite `src` (w x h, bridge-packed) onto the device so that
    // src(0,0) lands at (dx,dy); only pixels inside `clip` (and bounds)
    // are touched. Implemented via XER::Bridge::Skia::ImageComposite so
    // all 29 named blend modes work. Must be called inside a
    // transaction for undoability (the caller opens one — painting
    // outside a transaction is legal but leaves no undo record).
    bool CompositeRect(const IntRect& clip, const std::vector<uint32_t>& src,
                       int src_w, int src_h, int dx, int dy,
                       const std::string& blend_mode, float opacity);

    // --- transactions / mementos (KisMementoManager analogue) -------
    // At most one transaction may be open. While open, the first write
    // to each tile snapshots the old tile into the memento.
    void BeginTransaction();
    // Closes the transaction and returns the captured delta (may be
    // empty). Caller owns it — this is what undo commands store.
    std::unique_ptr<TileMemento> EndTransaction();
    // Closes the transaction and restores the pre-transaction state.
    void AbortTransaction();
    bool InTransaction() const { return active_memento_ != nullptr; }

    // Swap the memento's recorded old state back into the device and
    // return the INVERSE memento (the state just replaced). Undo calls
    // this with the stroke memento; redo calls it with the returned
    // inverse — pixel-exact ping-pong, O(touched tiles).
    std::unique_ptr<TileMemento> ApplyMemento(const TileMemento& m);

    size_t TileCount() const { return tiles_.size(); }
    size_t TileMemoryBytes() const { return tiles_.size() * TileBytes(); }
    // Slow test/debug helper: per-pixel compare over Bounds().
    bool IdenticalPixels(const TiledDevice& other) const;

private:
    static constexpr int64_t KeyOf(int col, int row) {
        return (static_cast<int64_t>(col) << 32) |
               static_cast<uint32_t>(row);
    }
    size_t TileBytes() const {
        return static_cast<size_t>(kTileSize) * kTileSize * BytesPerPixel();
    }
    const Tile* TileAt(int col, int row) const;
    // Returns a mutable tile, performing the memento snapshot / COW
    // clone as required. Never nullptr (allocates on demand).
    Tile* TileForWrite(int col, int row);
    // The lazily built tile filled entirely with default_pixel_ —
    // shared by every "uniform default" tile so fills stay sparse.
    const std::shared_ptr<Tile>& DefaultTile();

    int width_;
    int height_;
    image::PixelFormat format_;
    Pixel default_pixel_;
    std::unordered_map<int64_t, std::shared_ptr<Tile>> tiles_;
    std::shared_ptr<Tile> default_tile_;
    // Owned delta of the open transaction (nullptr when none). Recorded
    // tiles are shared_ptr'd, so the memento keeps the old state even
    // when the device swaps in a clone.
    std::unique_ptr<TileMemento> active_memento_;
};

}  // namespace XER::VideoEditor::Photo
