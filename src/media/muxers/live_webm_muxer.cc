#include "media/muxers/live_webm_muxer.h"

#include <cstring>

namespace XER::Media {
namespace {

// EBML element id (already includes length-descriptor bits).
void PutId(std::vector<std::uint8_t>* v, std::uint32_t id) {
    if (id > 0xFFFF) {
        v->push_back(static_cast<std::uint8_t>(id >> 24));
        v->push_back(static_cast<std::uint8_t>(id >> 16));
    }
    if (id > 0xFF) v->push_back(static_cast<std::uint8_t>(id >> 8));
    v->push_back(static_cast<std::uint8_t>(id));
}

// EBML variable-length size; 0 means "unknown" (live streaming).
void PutSize(std::vector<std::uint8_t>* v, std::uint64_t size,
             int width = 0) {
    if (size == 0 && width == 0) {
        // unknown-size sentinel, 8 bytes
        v->insert(v->end(), {0x01, 0xFF, 0xFF, 0xFF,
                             0xFF, 0xFF, 0xFF, 0xFF});
        return;
    }
    if (width == 0)
        width = size < 0x7F ? 1 : size < 0x3FFF ? 2 : 4;
    for (int i = width - 1; i >= 0; --i) {
        std::uint8_t b = static_cast<std::uint8_t>(size >> (8 * i));
        if (i == width - 1)
            b |= static_cast<std::uint8_t>(1u << (8 - width));
        v->push_back(b);
    }
}

void PutUint(std::vector<std::uint8_t>* v, std::uint32_t id,
             std::uint64_t value, int width = 1) {
    PutId(v, id);
    PutSize(v, static_cast<std::uint64_t>(width));
    for (int i = width - 1; i >= 0; --i)
        v->push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

void PutString(std::vector<std::uint8_t>* v, std::uint32_t id,
               const char* s) {
    const auto n = static_cast<std::uint64_t>(std::strlen(s));
    PutId(v, id);
    PutSize(v, n);
    v->insert(v->end(), s, s + n);
}

void PutElement(std::vector<std::uint8_t>* v, std::uint32_t id,
                const std::vector<std::uint8_t>& payload) {
    PutId(v, id);
    PutSize(v, payload.size());
    v->insert(v->end(), payload.begin(), payload.end());
}

const char* VideoCodecString(CodecId c) {
    switch (c) {
        case CodecId::kVP8:  return "V_VP8";
        case CodecId::kVP9:  return "V_VP9";
        case CodecId::kAV1:  return "V_AV1";
        case CodecId::kH264: return "V_MPEG4/ISO/AVC";
        case CodecId::kH265: return "V_MPEGH/ISO/HEVC";
        default:             return "V_VP8";
    }
}

std::vector<std::uint8_t> BuildTrackEntry(std::uint8_t number,
                                        bool is_video,
                                        const char* codec_id) {
    std::vector<std::uint8_t> t;
    PutUint(&t, 0xD7, number);        // TrackNumber
    PutUint(&t, 0x73C5, number, 1);   // TrackUID
    PutUint(&t, 0x83, is_video ? 1 : 2);  // TrackType
    PutString(&t, 0x86, codec_id);    // CodecID
    return t;
}

}  // namespace

std::vector<std::uint8_t> LiveWebmMuxer::BuildHeader(bool has_video,
                                                   CodecId video_codec,
                                                   bool has_audio) {
    std::vector<std::uint8_t> h;
    // EBML header (0x1A45DFA3).
    std::vector<std::uint8_t> ebml;
    PutUint(&ebml, 0x4286, 1);              // EBMLVersion
    PutUint(&ebml, 0x42F7, 1);              // EBMLReadVersion
    PutUint(&ebml, 0x42F2, 4);              // EBMLMaxIDLength
    PutUint(&ebml, 0x42F3, 8);              // EBMLMaxSizeLength
    PutString(&ebml, 0x4282, "webm");       // DocType
    PutUint(&ebml, 0x4287, 2);              // DocTypeVersion
    PutUint(&ebml, 0x4285, 2);              // DocTypeReadVersion
    PutElement(&h, 0x1A45DFA3, ebml);

    // Segment (0x18538067) with unknown size — live stream.
    PutId(&h, 0x18538067);
    PutSize(&h, 0);

    // Tracks (0x1654AE6B).
    std::vector<std::uint8_t> tracks;
    if (has_video) {
        PutElement(&tracks, 0xAE,
                   BuildTrackEntry(1, true,
                                   VideoCodecString(video_codec)));
    }
    if (has_audio)
        PutElement(&tracks, 0xAE,
                   BuildTrackEntry(2, false, "A_OPUS"));
    PutElement(&h, 0x1654AE6B, tracks);

    // Open Cluster (0x1F43B675) unknown size + Timecode 0.
    PutId(&h, 0x1F43B675);
    PutSize(&h, 0);
    PutUint(&h, 0xE7, 0, 4);  // Timecode
    return h;
}

bool LiveWebmMuxer::Open(Container container, const std::string&,
                         std::string* error_out) {
    if (container != Container::kWebm) {
        if (error_out)
            *error_out =
                "LiveWebmMuxer only supports webm (FFmpegMuxer for "
                "mkv/mp4)";
        return false;
    }
    open_ = true;
    header_sent_ = false;
    return true;
}

bool LiveWebmMuxer::AddStream(StreamType type, CodecId codec,
                              std::string* error_out) {
    if (!open_) {
        if (error_out) *error_out = "muxer not open";
        return false;
    }
    if (!ContainerSupportsCodec(Container::kWebm, codec)) {
        if (error_out) *error_out = "codec not allowed in webm";
        return false;
    }
    if (type == StreamType::kVideo) {
        has_video_ = true;
        video_codec_ = codec;
    } else if (type == StreamType::kAudio) {
        has_audio_ = true;
    }
    return true;
}

void LiveWebmMuxer::Emit(const std::vector<std::uint8_t>& bytes) {
    if (out_)
        out_(bytes.data(), bytes.size());
    else
        buffered_.insert(buffered_.end(), bytes.begin(), bytes.end());
}

bool LiveWebmMuxer::WritePacket(const MuxedPacket& packet,
                                std::string* error_out) {
    if (!open_) {
        if (error_out) *error_out = "muxer not open";
        return false;
    }
    if (!header_sent_) {
        Emit(BuildHeader(has_video_, video_codec_, has_audio_));
        header_sent_ = true;
    }
    const TimeTicks t =
        timestamps_.Normalize(packet.type, packet.timestamp);
    // SimpleBlock (0xA3): track|0x80, int16 timecode ms, flags, payload.
    std::vector<std::uint8_t> block;
    const std::uint8_t track =
        packet.type == StreamType::kVideo ? 1 : 2;
    block.push_back(static_cast<std::uint8_t>(0x80 | track));
    const auto tc_ms = static_cast<std::int16_t>(t / 1000);
    block.push_back(static_cast<std::uint8_t>(tc_ms >> 8));
    block.push_back(static_cast<std::uint8_t>(tc_ms));
    block.push_back(packet.type == StreamType::kVideo ? 0x80 : 0x00);
    block.insert(block.end(), packet.data.begin(), packet.data.end());
    std::vector<std::uint8_t> out;
    PutElement(&out, 0xA3, block);
    Emit(out);
    return true;
}

bool LiveWebmMuxer::Close(std::string*) {
    open_ = false;
    return true;
}

}  // namespace XER::Media
