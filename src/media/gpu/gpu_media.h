/**
 * XER::Media GPU-process decode/encode acceleration (design doc).
 * AcceleratedVideoDecoder implementations exist per codec; the
 * GpuVideoAcceleratorFactory produces VideoDecodeAccelerator instances for
 * the IPC layer. CommandBufferHelper operates inside a renderer's command
 * buffer from the GPU process.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "media/decoders/decoder.h"
#include "media/base/media_types.h"

namespace XER::Media {

// Hardware-accelerated decoder; implementations per codec:
// AV1Decoder, H264Decoder, H265Decoder, VP8Decoder, VP9Decoder.
class AcceleratedVideoDecoder : public VideoDecoder {
public:
    // Codec this implementation handles.
    virtual CodecId accelerated_codec() const = 0;
    // Capabilities advertised to the factory/IPC layer.
    virtual bool HardwareAvailable() const = 0;
};

// Abstraction over the platform video-decode accelerator consumed by the
// IPC layer (DXVA/D3D11VA on Windows).
class VideoDecodeAccelerator {
public:
    virtual ~VideoDecodeAccelerator() = default;
    virtual bool Initialize(CodecId codec, std::string* error_out) = 0;
    virtual std::unique_ptr<AcceleratedVideoDecoder> CreateDecoder() = 0;
    virtual bool supports(CodecId codec) const = 0;
};

// Provides VideoDecodeAccelerator implementations to the IPC layer.
class GpuVideoAcceleratorFactory {
public:
    static GpuVideoAcceleratorFactory& Instance();

    // First accelerator supporting |codec| wins, mirroring decoder order.
    VideoDecodeAccelerator* AcceleratorFor(CodecId codec);
    void RegisterAccelerator(std::unique_ptr<VideoDecodeAccelerator> accel);
    std::vector<CodecId> SupportedCodecs() const;

private:
    std::vector<std::unique_ptr<VideoDecodeAccelerator>> accelerators_;
};

// Utilities for operating inside a renderer's CommandBuffer from the GPU
// process (shared-image mailboxes, sync tokens).
class CommandBufferHelper {
public:
    virtual ~CommandBufferHelper() = default;
    // Waits for a sync token then returns true when the mailbox texture is
    // safe to sample/read back.
    virtual bool WaitSyncToken(std::uint64_t token) = 0;
    virtual bool ReadbackMailbox(std::uint64_t mailbox, int width, int height,
                                 std::vector<std::uint8_t>* rgba) = 0;
};

}  // namespace XER::Media
