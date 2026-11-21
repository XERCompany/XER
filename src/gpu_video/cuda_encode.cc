// cuda_encode.cc — see cuda_encode.h for the contract. Pure
// probe/resolver: encoder registration comes from libavcodec, driver
// presence from LoadLibrary probes, and the ground-truth "will NVENC
// actually run" check is av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_CUDA)
// — the same init NVENC performs under the hood at codec open.
#include "gpu_video/cuda_encode.h"

#include "gpu_video/cuda_runtime.h"

#ifdef _WIN32
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}
#endif

#include <algorithm>
#include <cctype>

namespace XER::GpuVideo {

namespace {

#ifdef _WIN32

bool CudaDeviceCreatable() {
    AVBufferRef* dev = nullptr;
    const bool ok =
        av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_CUDA, nullptr,
                               nullptr, 0) == 0 &&
        dev != nullptr;
    av_buffer_unref(&dev);
    return ok;
}

const CudaEncodeProbe& ComputeProbe() {
    static const CudaEncodeProbe probe = [] {
        CudaEncodeProbe p;
        p.encoder_h264_nvenc =
            avcodec_find_encoder_by_name("h264_nvenc") != nullptr;
        p.encoder_hevc_nvenc =
            avcodec_find_encoder_by_name("hevc_nvenc") != nullptr;
        p.encoder_av1_nvenc =
            avcodec_find_encoder_by_name("av1_nvenc") != nullptr;
        p.nvcuda_dll = detail::CudaDriverLoadable();
        p.nvenc_api_dll = detail::NvencApiLoadable();
        p.cuda_device = CudaDeviceCreatable();
        if (!p.HasAnyEncoder()) {
            p.detail = "ffmpeg build lacks *_nvenc encoders";
        } else if (!p.nvcuda_dll) {
            p.detail = "CUDA driver library not loadable (no NVIDIA "
                       "driver?)";
        } else if (!p.nvenc_api_dll) {
            p.detail = "nvEncodeAPI runtime not loadable";
        } else if (!p.cuda_device) {
            p.detail = "CUDA device init failed (driver present, no "
                       "usable GPU?)";
        } else {
            p.detail = "nvenc ready";
        }
        return p;
    }();
    return probe;
}

#else  // !_WIN32

const CudaEncodeProbe& ComputeProbe() {
    static const CudaEncodeProbe probe = [] {
        CudaEncodeProbe p;
        p.nvcuda_dll = detail::CudaDriverLoadable();
        p.nvenc_api_dll = detail::NvencApiLoadable();
        p.detail = "video encode unsupported on this platform";
        return p;
    }();
    return probe;
}

#endif

std::string LowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Maps an accepted NVENC request to the concrete FFmpeg encoder name.
// `requested` is already lowercased. Returns "" when the request is
// NVENC-flavored but the wanted encoder is not registered.
std::string MapNvencName(const std::string& requested,
                         const CudaEncodeProbe& probe) {
    if (requested == "nvenc") {
        if (probe.encoder_h264_nvenc) return "h264_nvenc";
        if (probe.encoder_hevc_nvenc) return "hevc_nvenc";
        if (probe.encoder_av1_nvenc) return "av1_nvenc";
        return "";
    }
    if (requested == "nvenc_h264") return probe.encoder_h264_nvenc ? "h264_nvenc" : "";
    if (requested == "nvenc_hevc") return probe.encoder_hevc_nvenc ? "hevc_nvenc" : "";
    if (requested == "nvenc_av1") return probe.encoder_av1_nvenc ? "av1_nvenc" : "";
    if (requested == "h264_nvenc") return probe.encoder_h264_nvenc ? "h264_nvenc" : "";
    if (requested == "hevc_nvenc") return probe.encoder_hevc_nvenc ? "hevc_nvenc" : "";
    if (requested == "av1_nvenc") return probe.encoder_av1_nvenc ? "av1_nvenc" : "";
    return "";
}

bool IsNvencRequest(const std::string& lowered) {
    return lowered == "nvenc" ||
           lowered.rfind("nvenc_", 0) == 0 ||
           lowered.find("_nvenc") != std::string::npos;
}

}  // namespace

const CudaEncodeProbe& ProbeCudaEncode() {
    return ComputeProbe();
}

std::vector<std::string> CudaEncodeProbe::Encoders() const {
    std::vector<std::string> out;
    if (encoder_h264_nvenc) out.emplace_back("h264_nvenc");
    if (encoder_hevc_nvenc) out.emplace_back("hevc_nvenc");
    if (encoder_av1_nvenc) out.emplace_back("av1_nvenc");
    return out;
}

std::string CudaEncodeProbe::Report() const {
    std::string s;
    const auto line = [&s](const char* k, bool v) {
        s += "  ";
        s += k;
        s += v ? ": yes\n" : ": no\n";
    };
    line("encoder h264_nvenc", encoder_h264_nvenc);
    line("encoder hevc_nvenc", encoder_hevc_nvenc);
    line("encoder av1_nvenc", encoder_av1_nvenc);
    line("nvcuda driver dll", nvcuda_dll);
    line("nvEncodeAPI dll", nvenc_api_dll);
    line("cuda device ctx", cuda_device);
    s += "  cuda_encode available: ";
    s += Available() ? "yes" : "no";
    s += "  (";
    s += detail;
    s += ")\n";
    return s;
}

std::string ResolveVideoCodecName(const std::string& requested,
                                  std::string* note_out,
                                  const std::string& fallback) {
    if (note_out != nullptr) {
        note_out->clear();
    }
    const std::string lowered = LowerAscii(requested);
    if (!IsNvencRequest(lowered)) {
        return requested;   // software name — passthrough
    }
    const CudaEncodeProbe& probe = ProbeCudaEncode();
    const std::string mapped = MapNvencName(lowered, probe);
    if (!mapped.empty() && probe.cuda_device) {
        return mapped;
    }
    // NVENC was explicitly requested but cannot run here — degrade to
    // the software fallback with an explanation rather than failing
    // the export outright (graceful-degradation contract).
    if (note_out != nullptr) {
        if (mapped.empty()) {
            *note_out = "requested encoder '" + requested +
                        "' is not in this FFmpeg build";
        } else {
            *note_out = "encoder '" + mapped +
                        "' cannot initialize here (" + probe.detail +
                        ")";
        }
        if (!fallback.empty()) {
            *note_out += " — falling back to '" + fallback + "'";
        }
    }
    return fallback;
}

std::vector<std::string> AvailableNvencPresets() {
    const CudaEncodeProbe& probe = ProbeCudaEncode();
    if (!probe.Available()) {
        return {};
    }
    return probe.Encoders();
}

}  // namespace XER::GpuVideo
