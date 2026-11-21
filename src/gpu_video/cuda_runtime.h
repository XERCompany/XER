// cuda_runtime.h — delay-loaded NVIDIA driver probes shared by the
// gpu_video modules (YA.CUDADecode / YA.CUDAEncode). Header-only and
// internal to this directory: the public module headers must stay
// FFmpeg-free so consumers (media_decoder, export_engine, smoke
// checks) never see libav* types.
//
// Same contract as async_io/hardware_abstraction/nvml_loader.cc: the
// NVIDIA user-mode libraries are LoadLibrary/dlopen-probed at run
// time. Nothing in this tree links cuda.lib / nvml.lib / nvenc.lib —
// a machine without the driver stack reports "unavailable" and the
// process keeps running. FFmpeg's own hwaccel/nvenc/cuvid code does
// the same dlopen internally, so probing here is only for the
// availability matrix — the authoritative test is
// av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_CUDA) succeeding.
#pragma once

#include <string>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace XER::GpuVideo::detail {

// True when `name` can be loaded right now. The handle is released
// immediately — this answers "driver file exists and resolves", not
// "hold it open" (FFmpeg re-loads what it needs itself).
inline bool RuntimeDllLoadable(const char* name) {
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(name);
    if (h == nullptr) return false;
    FreeLibrary(h);
    return true;
#else
    void* h = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
    if (h == nullptr) return false;
    dlclose(h);
    return true;
#endif
}

// CUDA driver (nvcuda.dll / libcuda.so.1): present iff an NVIDIA
// display driver is installed. Its absence is the cheapest "no CUDA
// on this machine" signal and needs no GPU query.
inline bool CudaDriverLoadable() {
#if defined(_WIN32)
    return RuntimeDllLoadable("nvcuda.dll");
#else
    return RuntimeDllLoadable("libcuda.so.1") ||
           RuntimeDllLoadable("libcuda.so");
#endif
}

// NVDEC video-decode library (nvcuvid.dll / libnvcuvid.so) — used by
// the *_cuvid decoders; informational for the probe matrix since the
// hwaccel path goes through nvcuda instead.
inline bool NvcuvidLoadable() {
#if defined(_WIN32)
    return RuntimeDllLoadable("nvcuvid.dll");
#else
    return RuntimeDllLoadable("libnvcuvid.so.1") ||
           RuntimeDllLoadable("libnvcuvid.so");
#endif
}

// NVENC encode library (nvEncodeAPI64.dll / libnvidia-encode.so) —
// what the *_nvenc encoders dlopen at codec-open time.
inline bool NvencApiLoadable() {
#if defined(_WIN32)
    return RuntimeDllLoadable("nvEncodeAPI64.dll");
#else
    return RuntimeDllLoadable("libnvidia-encode.so.1") ||
           RuntimeDllLoadable("libnvidia-encode.so");
#endif
}

}  // namespace XER::GpuVideo::detail
