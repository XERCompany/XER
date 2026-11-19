/**
 * ChunkDemuxerImpl — Media Source Extensions path (design doc). The host
 * appends container segments; a pluggable InitParser maps the init segment
 * to MediaTracks, and a pluggable ChunkParser pulls coded units from the
 * byte queue into per-stream read queues.
 *
 * The default parsers handle raw elementary appends: RegisterRawStream()
 * exposes a stream immediately and AppendData() feeds it 1:1 — enough for
 * MSE-shaped producers that already emit coded frames. A WebM/MP4 box
 * parser can be injected via SetParser().
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "media/demuxer/byte_queue.h"
#include "media/demuxer/demuxer.h"
#include "media/demuxer/media_track.h"

namespace XER::Media {

class ChunkDemuxerImpl final : public ChunkDemuxer {
public:
    // Returns true when a complete coded unit was emitted for |track_id|.
    using UnitCallback = std::function<bool(std::uint32_t track_id,
                                            DemuxerStream::ReadResult unit)>;

    // Parser hook: called on init-segment appends to declare tracks.
    using InitParser =
        std::function<bool(const std::vector<std::uint8_t>& init,
                           std::vector<MediaTrack>* tracks_out,
                           std::string* error)>;

    ChunkDemuxerImpl();
    ~ChunkDemuxerImpl() override;  // out-of-line: ChunkStream is incomplete here

    void SetInitParser(InitParser parser) { init_parser_ = std::move(parser); }
    void SetUnitCallback(UnitCallback cb) { unit_cb_ = std::move(cb); }

    // Raw-stream mode: declare a track up-front; appended bytes become
    // coded units for it.
    std::uint32_t RegisterRawStream(const MediaTrack& track);

    // Demuxer.
    bool Open(const std::string& path, std::string* error_out) override;
    void Close() override;
    bool IsOpen() const override { return open_; }
    const std::vector<DemuxerStream*>& streams() const override;
    bool Seek(TimeTicks position, std::string* error_out) override;
    TimeTicks duration() const override { return duration_; }

    // ChunkDemuxer.
    std::int64_t AppendData(const std::uint8_t* data,
                            std::size_t size) override;
    void Flush() override;
    void ResetParserState() override;

    std::vector<MediaTrack> tracks() const;
    // Container detected by StreamParser::Sniff on the first append.
    const std::string& container() const { return container_; }

private:
    class ChunkStream;
    void PumpQueue();

    ByteQueue queue_;
    std::vector<std::unique_ptr<ChunkStream>> owned_streams_;
    std::vector<DemuxerStream*> streams_;
    std::unordered_map<std::uint32_t, MediaTrack> tracks_;
    InitParser init_parser_;
    UnitCallback unit_cb_;
    std::string container_;  // sniffed: webm/mp4/wav/ogg/raw
    TimeTicks duration_ = kNoTimestamp;
    bool open_ = false;
    bool init_seen_ = false;
};

}  // namespace XER::Media
