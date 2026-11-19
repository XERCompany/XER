#include "media/demuxer/chunk_demuxer.h"

#include <mutex>
#include <utility>

#include "media/demuxer/stream_parser.h"

namespace XER::Media {

// Stream whose reads pop queued coded units; when the unit callback is
// unset the byte queue feeds raw streams 1:1.
class ChunkDemuxerImpl::ChunkStream final : public DemuxerStream {
public:
    ChunkStream(ChunkDemuxerImpl* owner, MediaTrack track)
        : owner_(owner), track_(std::move(track)) {}

    StreamType type() const override { return track_.type; }
    CodecId codec() const override { return track_.codec; }

    void Read(ReadCallback cb) override {
        std::lock_guard lock(mu_);
        if (!pending_.empty()) {
            auto r = std::move(pending_.front());
            pending_.erase(pending_.begin());
            cb(std::move(r));
            return;
        }
        ReadResult r;
        r.end_of_stream = eos_;
        cb(std::move(r));
    }

    void Push(ReadResult r) {
        std::lock_guard lock(mu_);
        pending_.push_back(std::move(r));
    }
    void SetEos() {
        std::lock_guard lock(mu_);
        eos_ = true;
    }

private:
    ChunkDemuxerImpl* owner_;
    MediaTrack track_;
    std::mutex mu_;
    std::vector<ReadResult> pending_;
    bool eos_ = false;
};

ChunkDemuxerImpl::ChunkDemuxerImpl() = default;
ChunkDemuxerImpl::~ChunkDemuxerImpl() = default;

std::uint32_t ChunkDemuxerImpl::RegisterRawStream(const MediaTrack& track) {
    const std::uint32_t id = track.id ? track.id
                                      : static_cast<std::uint32_t>(
                                            owned_streams_.size() + 1);
    MediaTrack t = track;
    t.id = id;
    tracks_[id] = t;
    owned_streams_.push_back(std::make_unique<ChunkStream>(this, t));
    streams_.push_back(owned_streams_.back().get());
    return id;
}

bool ChunkDemuxerImpl::Open(const std::string&, std::string*) {
    open_ = true;
    return true;
}

void ChunkDemuxerImpl::Close() {
    open_ = false;
    queue_.Clear();
}

const std::vector<DemuxerStream*>& ChunkDemuxerImpl::streams() const {
    return streams_;
}

bool ChunkDemuxerImpl::Seek(TimeTicks, std::string*) {
    ResetParserState();
    return true;
}

std::int64_t ChunkDemuxerImpl::AppendData(const std::uint8_t* data,
                                        std::size_t size) {
    if (!open_ || !data || size == 0) return -1;
    queue_.Append(data, size);
    PumpQueue();
    return static_cast<std::int64_t>(size);
}

void ChunkDemuxerImpl::PumpQueue() {
    if (queue_.empty()) return;

    // Init segment: first append when an init parser is installed.
    if (!init_seen_ && init_parser_ && streams_.empty()) {
        std::vector<std::uint8_t> init(queue_.size());
        queue_.Peek(init.data(), init.size());
        // Auto-detect container via StreamParser::Sniff; a parser that
        // rejects the format leaves init unseen so later appends retry.
        const char* sniffed =
            StreamParser::Sniff(init.data(), init.size());
        container_ = sniffed;
        std::vector<MediaTrack> tracks;
        std::string err;
        if (init_parser_(init, &tracks, &err)) {
            for (const auto& t : tracks) RegisterRawStream(t);
            queue_.Clear();
            init_seen_ = true;
        }
        return;  // wait for a complete parse before consuming more
    }

    if (streams_.empty()) return;  // no consumers yet — keep buffering

    // Feed the unit callback or the first raw stream.
    DemuxerStream::ReadResult unit;
    unit.ok = true;
    unit.data.resize(queue_.size());
    queue_.Peek(unit.data.data(), unit.data.size());
    queue_.Clear();
    if (unit_cb_) {
        unit_cb_(0, unit);
        return;
    }
    static_cast<ChunkStream*>(streams_.front())->Push(std::move(unit));
}

void ChunkDemuxerImpl::Flush() { PumpQueue(); }

void ChunkDemuxerImpl::ResetParserState() {
    queue_.Clear();
    init_seen_ = false;
}

std::vector<MediaTrack> ChunkDemuxerImpl::tracks() const {
    std::vector<MediaTrack> out;
    out.reserve(tracks_.size());
    for (const auto& [id, t] : tracks_) out.push_back(t);
    return out;
}

}  // namespace XER::Media
