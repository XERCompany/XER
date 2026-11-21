// YA.CUDAEncode — NVENC encode seam for the XER media pipeline. The
// FFmpeg build carries h264_nvenc / hevc_nvenc / av1_nvenc when the
// encoder list reports them; NVENC itself dlopens nvEncodeAPI64.dll +
// nvcuda at codec-open time, so this module is a pure probe/resolver —
// zero CUDA link dependency, GPU-less machines report
// available:false and callers stay on software encoders.
//
// Wiring model: export paths keep calling
// FFmpeg::EncodeVideoFrameStream / EncodeAudioVideoStream unchanged —
// the only decision point is the codec NAME. ResolveVideoCodecName()
// maps a requested name ("nvenc" alias, or an explicit *_nvenc) to the
// concrete encoder that can actually run here, falling back to a
// software name with a note when NVENC is unusable.
#pragma once

#include <string>
#include <vector>

namespace XER::GpuVideo {

// One-shot availability matrix for the NVENC encode path — layered
// fields so --smoke/--diag can show WHY a machine does or does not
// qualify.
struct CudaEncodeProbe {
    bool encoder_h264_nvenc = false;  // registered in this FFmpeg build
    bool encoder_hevc_nvenc = false;
    bool encoder_av1_nvenc = false;
    bool nvcuda_dll = false;          // CUDA driver library loadable
    bool nvenc_api_dll = false;       // nvEncodeAPI64.dll loadable
    bool cuda_device = false;         // CUDA device context creatable
    std::string detail;

    // Registered encoders — subset of {h264_nvenc, hevc_nvenc,
    // av1_nvenc} present in this FFmpeg build.
    std::vector<std::string> Encoders() const;
    bool HasAnyEncoder() const {
        return encoder_h264_nvenc || encoder_hevc_nvenc ||
               encoder_av1_nvenc;
    }
    // The NVENC path is usable iff at least one *_nvenc encoder is
    // compiled in AND a CUDA device context can be created (driver +
    // GPU — nvEncodeAPI64.dll rides the driver install).
    bool Available() const {
        return HasAnyEncoder() && cuda_device;
    }
    std::string Report() const;
};

// Cached probe — same rationale as ProbeCudaDecode.
const CudaEncodeProbe& ProbeCudaEncode();

// Resolves a requested encoder name to the name that should be handed
// to the FFmpeg encode bridge:
//   * non-NVENC names ("libx264", "aac", ...) pass through unchanged;
//   * "nvenc" / "nvenc_h264" / "nvenc_hevc" / "nvenc_av1" aliases pick
//     the matching registered NVENC encoder (bare "nvenc" prefers
//     h264 > hevc > av1);
//   * an explicit *_nvenc name is honored when registered AND a CUDA
//     device is usable;
//   * when NVENC was requested but cannot run here, `fallback`
//     (default "libx264") is returned and `note_out` explains the
//     downgrade — callers may surface it in errors/logs. A null/empty
//     `fallback` returns "" instead (hard "unavailable" signal).
// An empty return also means "could not resolve".
std::string ResolveVideoCodecName(const std::string& requested,
                                  std::string* note_out = nullptr,
                                  const std::string& fallback =
                                      "libx264");

// Convenience for UI/preset surfaces: the NVENC encoder names a
// "hardware" codec picker should offer here — empty when NVENC is
// unusable on this machine.
std::vector<std::string> AvailableNvencPresets();

}  // namespace XER::GpuVideo
