/**
 * DemuxerFactory — picks the Demuxer implementation per the design doc:
 * FFmpegDemuxer for src= playback, ChunkDemuxerImpl for MSE.
 */
#pragma once

#include <memory>
#include <string>

#include "media/demuxer/demuxer.h"

namespace XER::Media {

// True for MSE-style sources (mediasource URIs or explicit MSE flag).
bool IsMediaSourceUri(const std::string& uri);

// Creates the appropriate demuxer for |uri|; may return nullptr when no
// backend supports it.
std::unique_ptr<Demuxer> CreateDemuxerFor(const std::string& uri);

// Creates a ChunkDemuxerImpl for MSE usage.
std::unique_ptr<ChunkDemuxer> CreateChunkDemuxer();

}  // namespace XER::Media
