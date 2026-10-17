#include "clip_frame_cache.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <absl/strings/str_cat.h>
#include <zlib.h>

#include "cache/cache_entry.h"

namespace video {
namespace {

constexpr std::uint32_t kMagic = 0x46435844u;  // DXCF little-endian
constexpr std::uint32_t kVersion = 1u;

struct PackedFrameHeader {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pixel_format = 0;
    std::uint64_t pts = 0;
    std::uint64_t raw_size = 0;
    std::uint64_t compressed_size = 0;
};

static_assert(sizeof(PackedFrameHeader) == 48u);

bool CheckedUint32(int value, std::uint32_t* out) {
    if (out == nullptr || value < 0) return false;
    *out = static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace

ClipFrameCache::ClipFrameCache(std::filesystem::path root, std::string key, std::uint32_t ttl_seconds)
    : storage_(std::move(root)), key_(std::move(key)), ttl_seconds_(ttl_seconds) {}

Engine::Cache::CacheEntry ClipFrameCache::EntryFor(std::size_t frame_index) const {
    return Engine::Cache::MakeEntryWithTtl(
        "video_music_bass_eq_frames",
        key_,
        absl::StrCat("frame_", frame_index, ".dxcfz"),
        ttl_seconds_);
}

bool ClipFrameCache::WriteFrame(std::size_t frame_index, const Frame& frame, std::string* error_out) {
    if (!frame.IsValid() || frame.Format() != PixelFormat::BGRA8) {
        if (error_out != nullptr) *error_out = "clip frame cache only accepts valid BGRA8 frames";
        return false;
    }

    PackedFrameHeader header;
    if (!CheckedUint32(frame.Width(), &header.width) || !CheckedUint32(frame.Height(), &header.height)) {
        if (error_out != nullptr) *error_out = "clip frame cache received invalid dimensions";
        return false;
    }
    header.pixel_format = static_cast<std::uint32_t>(frame.Format());
    header.pts = static_cast<std::uint64_t>(std::max<std::int64_t>(0, frame.Pts()));
    header.raw_size = static_cast<std::uint64_t>(frame.DataSize());

    const uLongf max_compressed_size = compressBound(static_cast<uLong>(frame.DataSize()));
    if (max_compressed_size > static_cast<uLongf>(std::numeric_limits<std::size_t>::max() - sizeof(PackedFrameHeader))) {
        if (error_out != nullptr) *error_out = "clip frame cache compressed payload is too large";
        return false;
    }

    std::vector<std::uint8_t> bytes(sizeof(PackedFrameHeader) + static_cast<std::size_t>(max_compressed_size));
    uLongf compressed_size = max_compressed_size;
    const int result = compress2(
        bytes.data() + sizeof(PackedFrameHeader),
        &compressed_size,
        frame.Data(),
        static_cast<uLong>(frame.DataSize()),
        Z_BEST_SPEED);
    if (result != Z_OK) {
        if (error_out != nullptr) *error_out = absl::StrCat("clip frame cache zlib compress failed: ", result);
        return false;
    }

    header.compressed_size = static_cast<std::uint64_t>(compressed_size);
    std::memcpy(bytes.data(), &header, sizeof(header));
    bytes.resize(sizeof(PackedFrameHeader) + static_cast<std::size_t>(compressed_size));
    return storage_.WriteBinary(EntryFor(frame_index), bytes, error_out);
}

bool ClipFrameCache::ReadFrame(std::size_t frame_index, std::shared_ptr<Frame>* frame_out, std::string* error_out) const {
    if (frame_out == nullptr) {
        if (error_out != nullptr) *error_out = "clip frame cache output target is null";
        return false;
    }

    std::vector<std::uint8_t> bytes;
    if (!storage_.ReadBinary(EntryFor(frame_index), &bytes, error_out)) {
        return false;
    }
    if (bytes.size() < sizeof(PackedFrameHeader)) {
        if (error_out != nullptr) *error_out = "clip frame cache packet is truncated";
        return false;
    }

    PackedFrameHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != kMagic || header.version != kVersion || header.pixel_format != static_cast<std::uint32_t>(PixelFormat::BGRA8)) {
        if (error_out != nullptr) *error_out = "clip frame cache packet header is invalid";
        return false;
    }
    if (header.width == 0u || header.height == 0u || header.raw_size == 0u || header.compressed_size == 0u) {
        if (error_out != nullptr) *error_out = "clip frame cache packet sizes are invalid";
        return false;
    }
    if (sizeof(PackedFrameHeader) + header.compressed_size != bytes.size()) {
        if (error_out != nullptr) *error_out = "clip frame cache packet size mismatch";
        return false;
    }

    auto frame = std::make_shared<Frame>();
    if (!frame->Allocate(static_cast<int>(header.width), static_cast<int>(header.height), PixelFormat::BGRA8)) {
        if (error_out != nullptr) *error_out = "clip frame cache failed to allocate decoded frame";
        return false;
    }
    if (frame->DataSize() != static_cast<std::size_t>(header.raw_size)) {
        if (error_out != nullptr) *error_out = "clip frame cache decoded frame size mismatch";
        return false;
    }

    uLongf raw_size = static_cast<uLongf>(frame->DataSize());
    const int result = uncompress(
        frame->Data(),
        &raw_size,
        bytes.data() + sizeof(PackedFrameHeader),
        static_cast<uLong>(header.compressed_size));
    if (result != Z_OK || raw_size != frame->DataSize()) {
        if (error_out != nullptr) *error_out = absl::StrCat("clip frame cache zlib decompress failed: ", result);
        return false;
    }
    frame->SetPts(static_cast<std::int64_t>(header.pts));
    *frame_out = std::move(frame);
    return true;
}

bool ClipFrameCache::RemoveFrame(std::size_t frame_index, std::string* error_out) {
    return storage_.Remove(EntryFor(frame_index), error_out);
}

}  // namespace video
