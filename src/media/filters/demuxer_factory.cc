#include "media/filters/demuxer_factory.h"

#include "media/demuxer/chunk_demuxer.h"
#include "media/filters/ffmpeg_demuxer.h"

namespace XER::Media {

bool IsMediaSourceUri(const std::string& uri) {
    return uri.rfind("mediasource:", 0) == 0 || uri.rfind("mse:", 0) == 0 ||
           uri.rfind("blob:", 0) == 0;
}

std::unique_ptr<Demuxer> CreateDemuxerFor(const std::string& uri) {
    if (IsMediaSourceUri(uri))
        return std::make_unique<ChunkDemuxerImpl>();
    return std::make_unique<FFmpegDemuxer>();
}

std::unique_ptr<ChunkDemuxer> CreateChunkDemuxer() {
    return std::make_unique<ChunkDemuxerImpl>();
}

}  // namespace XER::Media
