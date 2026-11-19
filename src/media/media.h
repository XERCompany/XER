/**
 * XER::Media umbrella header — the media pipeline per
 * MediaDecoder&Encoder -.txt:
 *
 *   Demuxer (src/wrapper/ffmpeg, or ChunkDemuxer for MSE)
 *     -> DemuxerStream -> DecoderStream -> Audio/VideoDecoder
 *     -> Audio/VideoRenderer -> event-driven Audio/VideoRendererSink
 *
 * Plus: MediaSession/AudioFocusManager (audio focus), MediaRouter
 * (sinks/routes, Presentation API), Muxer + MuxerTimestampAdapter
 * (recording), GPU acceleration (AcceleratedVideoDecoder,
 * GpuVideoAcceleratorFactory, CommandBufferHelper), and Capture.
 */
#pragma once

#include "media/base/media_types.h"
#include "media/demuxer/demuxer.h"
#include "media/decoders/decoder.h"
#include "media/renderers/renderer.h"
#include "media/player/media_player.h"
#include "media/session/media_session.h"
#include "media/router/media_router.h"
#include "media/muxers/muxer.h"
#include "media/gpu/gpu_media.h"
#include "media/capture/capture.h"
#include "media/audio/audio_manager.h"
#include "media/audio/audio_output_device.h"
#include "media/audio/audio_input_device.h"
#include "media/audio/audio_system.h"
#include "media/service/media_service.h"
#include "media/base/audio_bus.h"
#include "media/base/audio_fifo.h"
#include "media/base/channel_mixer.h"
#include "media/base/seekable_buffer.h"
#include "media/cdm/decryptor.h"
#include "media/metrics/pipeline_statistics.h"
#include "media/metrics/watch_time_recorder.h"
#include "media/filters/ffmpeg_demuxer.h"
#include "media/filters/ffmpeg_decoders.h"

namespace XER::Media {

// Library version for diagnostics.
const char* MediaVersion();

}  // namespace XER::Media
