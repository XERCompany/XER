#pragma once
#include "frame.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include <string>
#include <vector>

namespace video::utils {

/// Convert PixelFormat enum to human-readable string.
std::string PixelFormatName(PixelFormat fmt);

/// True if the format has an alpha channel.
bool HasAlpha(PixelFormat fmt);

/// Bytes per pixel for packed formats (BGRA8/RGBA8), 0 for planar.
int BytesPerPixel(PixelFormat fmt);

/// True if the format can be copied/scaled as one packed 4-channel pixel stream.
bool IsPackedFourChannel(PixelFormat fmt);

/// Copy frame bytes when dimensions, format and storage size match.
bool CopyFramePixels(const Frame& src, Frame& dst);

/// Resize packed BGRA/RGBA frames through the Skia raster bridge.
bool ResizeFrameWithSkia(const Frame& src, Frame& dst);

/// List video codec names available via FFmpeg bridge.
std::vector<std::string> AvailableVideoCodecs();

/// Probe a media file via FFmpeg bridge (returns stream metadata).
bool ProbeFile(const std::string& path,
               engine::bridge::ffmpeg::MediaInfo* out_info,
               std::string* out_error = nullptr);

/// Blend two BGRA8 pixels using a named Skia blend mode.
/// Returns blended pixel as Skia uint32 (R<<24|G<<16|B<<8|A).
uint32_t BlendPixels(const uint8_t* dst_bgra, const uint8_t* src_bgra,
                     const std::string& skia_blend_mode = "src_over");

/// Clamp value to [lo, hi].
template<typename T>
T Clamp(T v, T lo, T hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

}  // namespace video::utils