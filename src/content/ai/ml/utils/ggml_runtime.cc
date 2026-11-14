#include "ggml_runtime.h"

#include <thread>

#if defined(XER_AI_HAS_GGML)
#include <ggml.h>
#include <ggml-backend.h>
#define XER_AI_GGML_RUNTIME_ENABLED 1
#else
#define XER_AI_GGML_RUNTIME_ENABLED 0
#endif

namespace XER::ML::Utils {

#if XER_AI_GGML_RUNTIME_ENABLED

namespace {

bool LoadBackendsOnce() {
    static const bool loaded = [] {
        ggml_backend_load_all();
        return true;
    }();
    return loaded;
}

} // namespace

#endif

GgmlRuntimeInfo ProbeGgmlRuntime() {
    GgmlRuntimeInfo info;
#if XER_AI_GGML_RUNTIME_ENABLED
    if (!LoadBackendsOnce()) {
        return info;
    }

    info.available = true;
    info.backend_count = static_cast<size_t>(ggml_backend_dev_count());
    for (size_t index = 0; index < info.backend_count; ++index) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(index);
        if (dev == nullptr) {
            continue;
        }

        GgmlBackendInfo backend;
        backend.name = ggml_backend_dev_name(dev) != nullptr
                           ? ggml_backend_dev_name(dev)
                           : "unknown";
        backend.description = ggml_backend_dev_description(dev) != nullptr
                                  ? ggml_backend_dev_description(dev)
                                  : "";
        switch (ggml_backend_dev_type(dev)) {
            case GGML_BACKEND_DEVICE_TYPE_CPU:
                ++info.cpu_count;
                break;
            case GGML_BACKEND_DEVICE_TYPE_GPU:
                ++info.gpu_count;
                backend.is_gpu = true;
                break;
            case GGML_BACKEND_DEVICE_TYPE_ACCEL:
                ++info.accel_count;
                backend.is_gpu = true;
                break;
            default:
                break;
        }
        info.backends.push_back(std::move(backend));
    }

    const unsigned int hw = std::thread::hardware_concurrency();
    info.recommended_threads = hw > 0U ? static_cast<size_t>(hw) : 4U;
#endif
    return info;
}

std::string GgmlRuntimeSummary() {
    const GgmlRuntimeInfo info = ProbeGgmlRuntime();
    if (!info.available) {
        return "ggml: unavailable";
    }

    std::string summary = "ggml: " + std::to_string(info.backend_count) +
                          " backends (cpu=" + std::to_string(info.cpu_count) +
                          ", gpu=" + std::to_string(info.gpu_count) +
                          ", accel=" + std::to_string(info.accel_count) + ")";
    for (const GgmlBackendInfo& backend : info.backends) {
        summary += " [" + backend.name + "]";
    }
    return summary;
}

} // namespace XER::ML::Utils
