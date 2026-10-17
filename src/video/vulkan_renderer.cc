#include "vulkan_renderer.h"
#include "video_utils.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include "flux/core/logger.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace video {

namespace {

bool ProbeVulkanRuntime() {
#if defined(_WIN32)
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) return false;
    using PfnVkEnumerateInstanceVersion = int32_t(WINAPI*)(uint32_t*);
    auto pfn = reinterpret_cast<PfnVkEnumerateInstanceVersion>(GetProcAddress(lib, "vkEnumerateInstanceVersion"));
    bool available = true;
    if (pfn) {
        uint32_t version = 0;
        available = pfn(&version) == 0 && version > 0;
    }
    FreeLibrary(lib);
    return available;
#else
    void* lib = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) return false;
    using PfnVkEnumerateInstanceVersion = int32_t (*)(uint32_t*);
    auto pfn = reinterpret_cast<PfnVkEnumerateInstanceVersion>(dlsym(lib, "vkEnumerateInstanceVersion"));
    bool available = true;
    if (pfn) {
        uint32_t version = 0;
        available = pfn(&version) == 0 && version > 0;
    }
    dlclose(lib);
    return available;
#endif
}

}  // namespace

static flux::core::Logger& Log() {
    static flux::core::Logger logger;
    return logger;
}

VulkanRenderer::~VulkanRenderer() { Shutdown(); }

bool VulkanRenderer::Initialize(const std::string& /*device_hint*/) {
    bool vulkan_available = ProbeVulkanRuntime();
    Log().Info("VulkanRenderer",
               std::string("Initializing Vulkan renderer. Vulkan runtime: ") +
               (vulkan_available ? "available" : "not available") +
               " | Skia: " + (engine::bridge::skia::IsAvailable() ? "available" : "not available") +
               " | " + engine::bridge::skia::Summary());
    ready_ = vulkan_available;
    return ready_;
}

void VulkanRenderer::Shutdown() {
    if (ready_) {
        Log().Info("VulkanRenderer", "Shutdown");
        ready_ = false;
    }
}

bool VulkanRenderer::Render(const Frame& src, RenderTarget& target) {
    if (!ready_ || !src.IsValid()) return false;
    Frame* dst = target.GetFrame();
    if (!dst || !dst->IsValid()) return false;

    if (utils::CopyFramePixels(src, *dst)) return true;
    return utils::ResizeFrameWithSkia(src, *dst);
}

}  // namespace video