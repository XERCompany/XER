#include "codec_metadata_id3.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace XER::Engine::Audio::CodecIO {

namespace {

uint32_t ReadBigEndianU32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24)
        | (static_cast<uint32_t>(data[1]) << 16)
        | (static_cast<uint32_t>(data[2]) << 8)
        | static_cast<uint32_t>(data[3]);
}

uint32_t ReadSynchsafeU32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0] & 0x7Fu) << 21)
        | (static_cast<uint32_t>(data[1] & 0x7Fu) << 14)
        | (static_cast<uint32_t>(data[2] & 0x7Fu) << 7)
        | static_cast<uint32_t>(data[3] & 0x7Fu);
}

void WriteBigEndianU32(std::vector<uint8_t>* out, uint32_t value) {
    out->push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out->push_back(static_cast<uint8_t>(value & 0xFFu));
}

void WriteSynchsafeU32(std::vector<uint8_t>* out, uint32_t value) {
    out->push_back(static_cast<uint8_t>((value >> 21) & 0x7Fu));
    out->push_back(static_cast<uint8_t>((value >> 14) & 0x7Fu));
    out->push_back(static_cast<uint8_t>((value >> 7) & 0x7Fu));
    out->push_back(static_cast<uint8_t>(value & 0x7Fu));
}

std::string DecodeTextFrame(const uint8_t* data, size_t bytes) {
    if (data == nullptr || bytes == 0) {
        return {};
    }
    const uint8_t encoding = data[0];
    const char* text_ptr = reinterpret_cast<const char*>(data + 1);
    const size_t text_bytes = bytes - 1;
    if (encoding == 0 || encoding == 3) {
        const auto end = std::find(text_ptr, text_ptr + text_bytes, '\0');
        return std::string(text_ptr, end);
    }
    return std::string(text_ptr, text_ptr + text_bytes);
}

void AppendTextFrame(std::vector<uint8_t>* out, const char id[4], const std::string& value) {
    if (value.empty()) {
        return;
    }
    std::vector<uint8_t> payload;
    payload.reserve(value.size() + 2u);
    payload.push_back(0u);
    payload.insert(payload.end(), value.begin(), value.end());
    payload.push_back(0u);
    out->insert(out->end(), id, id + 4);
    WriteBigEndianU32(out, static_cast<uint32_t>(payload.size()));
    out->push_back(0u);
    out->push_back(0u);
    out->insert(out->end(), payload.begin(), payload.end());
}

}  // namespace

bool MetadataId3::Parse(const uint8_t* data, size_t bytes, Id3Tag& tag) const {
    if (data == nullptr || bytes < 10 || std::memcmp(data, "ID3", 3) != 0) {
        return false;
    }

    tag = Id3Tag{};
    const uint8_t major_version = data[3];
    const uint32_t tag_size = ReadSynchsafeU32(data + 6);
    size_t cursor = 10u;
    const size_t limit = std::min(bytes, static_cast<size_t>(10u + tag_size));
    while (cursor + 10u <= limit) {
        const uint8_t* frame_header = data + static_cast<std::ptrdiff_t>(cursor);
        if (frame_header[0] == 0u) {
            break;
        }
        const uint32_t frame_size = major_version >= 4 ? ReadSynchsafeU32(frame_header + 4) : ReadBigEndianU32(frame_header + 4);
        cursor += 10u;
        if (cursor + frame_size > limit) {
            return false;
        }
        const std::string frame_id(reinterpret_cast<const char*>(frame_header), 4u);
        const std::string value = DecodeTextFrame(data + static_cast<std::ptrdiff_t>(cursor), frame_size);
        if (frame_id == "TIT2") {
            tag.title = value;
        } else if (frame_id == "TPE1") {
            tag.artist = value;
        } else if (frame_id == "TALB") {
            tag.album = value;
        }
        cursor += frame_size;
    }
    return !tag.title.empty() || !tag.artist.empty() || !tag.album.empty();
}

std::vector<uint8_t> MetadataId3::Build(const Id3Tag& tag) const {
    std::vector<uint8_t> frames;
    AppendTextFrame(&frames, "TIT2", tag.title);
    AppendTextFrame(&frames, "TPE1", tag.artist);
    AppendTextFrame(&frames, "TALB", tag.album);

    std::vector<uint8_t> out;
    out.reserve(frames.size() + 10u);
    out.insert(out.end(), {'I', 'D', '3', 3, 0, 0});
    WriteSynchsafeU32(&out, static_cast<uint32_t>(frames.size()));
    out.insert(out.end(), frames.begin(), frames.end());
    return out;
}

}  // namespace XER::Engine::Audio::CodecIO
