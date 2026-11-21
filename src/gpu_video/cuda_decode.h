// YA.CUDADecode — GPU-accelerated video decode seam for the XER media
// pipeline. Uses FFmpeg's `cuda` hwaccel (hw_device AV_HWDEVICE_TYPE_CUDA
// + AV_PIX_FMT_CUDA get_format negotiation + av_hwframe_transfer_data
// download) when an NVIDIA driver/GPU is present, and degrades to plain
// software decode inside the same session when it is not. Nothing here
// links the CUDA toolkit: the driver libraries are runtime-probed
// (cuda_runtime.h) and FFmpeg dlopens nvcuda/nvcuvid itself — the DLL
// starts and reports available:false on GPU-less machines.
//
// API mirrors tools/video_editor/media_decoder.h's IMediaDecoder
// contract (same Open/NextFrame/Seek/audio-tap semantics, same
// AudioFrameInfo) so IMediaDecoder factories can delegate: frames come
// back as video::Frame (packed BGRA8 — the GPU-surface variant is a
// documented seam, see LastFrameSurface()).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "video/frame.h"

namespace XER::Bridge::FFmpeg {
struct AudioFrameInfo;  // ffmpeg_engine_bridge.h — same type the
                        // IMediaDecoder audio tap returns.
}

namespace XER::GpuVideo {

// One-shot availability matrix for the CUDA decode path. Fields are
// deliberately layered so --smoke / --diag can show WHY a machine does
// or does not qualify instead of a bare yes/no.
struct CudaDecodeProbe {
    bool ffmpeg_hwaccel_cuda = false;   // AV_HWDEVICE_TYPE_CUDA exists in this FFmpeg build
    bool nvcuda_dll = false;            // CUDA driver library loadable
    bool nvcuvid_dll = false;           // NVDEC library loadable (info only)
    bool cuda_device = false;           // av_hwdevice_ctx_create succeeded
    bool decoder_h264_cuvid = false;    // *_cuvid decoders compiled in (info)
    bool decoder_hevc_cuvid = false;
    bool decoder_av1_cuvid = false;
    std::string detail;                 // human-readable short reason

    // The decode path is usable iff the FFmpeg build carries the cuda
    // hwaccel AND a CUDA device context can actually be created (driver
    // + GPU present). nvcuvid/cuvid fields are informational extras.
    bool Available() const {
        return ffmpeg_hwaccel_cuda && cuda_device;
    }
    // Multi-line matrix for --smoke/--diag output.
    std::string Report() const;
};

// Cached probe — creating a CUDA hw device context is not free, so the
// first call computes the matrix and later calls return the snapshot.
const CudaDecodeProbe& ProbeCudaDecode();

// Where the decoded picture came from before it was downloaded to the
// returned CPU buffer. The video::Frame returned by NextFrame() is
// always CPU BGRA today; kCuda marks frames that lived on the GPU and
// went through av_hwframe_transfer_data — the seam where a future
// zero-copy GPU-surface variant plugs in without changing the API.
enum class FrameSurface { kNone = 0, kCpu, kCuda };

// Streaming video decode session producing video::Frame (BGRA8).
// Contract mirrors XER::Bridge::FFmpeg::VideoDecoder / IMediaDecoder:
//   * EnableAudioTap() must be called BEFORE Open() to route the
//     container's audio stream into the tap queue;
//   * NextFrame() decodes forward and reports media time via
//     GetPositionSeconds() after each successful call;
//   * audio-only containers still Open() — NextFrame() pumps the demux
//     and returns false, drive ReadAudioFrames() until AudioDrained();
//   * Seek() clamps to the stream bounds and lands on a keyframe at or
//     before the target;
//   * NOT internally synchronized — one consumer thread (audio tap
//     reads may run on a second thread, like the bridge contract).
// CUDA specifics: Open() creates a CUDA hw device context when the
// driver/GPU allows and lets the decoder negotiate AV_PIX_FMT_CUDA;
// if negotiation fails the same session decodes in software, so the
// caller's worst case is the plain CPU path. HwaccelActive() reports
// which path the open session actually took.
class CudaVideoDecoder {
public:
    CudaVideoDecoder();
    ~CudaVideoDecoder();  // Close()
    CudaVideoDecoder(const CudaVideoDecoder&) = delete;
    CudaVideoDecoder& operator=(const CudaVideoDecoder&) = delete;

    void EnableAudioTap(bool enable = true);

    bool Open(const std::string& utf8_path,
              std::string* error_out = nullptr);
    void Close();
    bool IsOpen() const;

    // Decodes the next frame into `out` as packed BGRA8 (GPU frames are
    // downloaded first). False at EOF / fatal error. `out` is required.
    bool NextFrame(video::Frame* out);
    bool Seek(double seconds);
    double GetPositionSeconds() const;
    double GetFps() const;
    double GetDurationSeconds() const;
    int GetWidth() const;    // 0 = audio-only container
    int GetHeight() const;

    // ---- audio tap (same contract as IMediaDecoder) ----
    bool HasAudio() const;
    double GetAudioStartSeconds() const;
    double GetAudioTimeBase() const;
    bool ReadAudioFrames(
        std::vector<XER::Bridge::FFmpeg::AudioFrameInfo>* out,
        int max_frames);
    bool AudioDrained() const;
    std::uint64_t GetAudioEpoch() const;

    // ---- GPU specifics ----
    // True when the open session negotiated AV_PIX_FMT_CUDA — i.e.
    // decoded frames actually lived on the GPU before download.
    bool HwaccelActive() const;
    // Surface of the most recently delivered frame.
    FrameSurface LastFrameSurface() const;
    // "cuda" when hwaccel is live, "" for the software path.
    std::string ActiveHwaccelName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::GpuVideo
