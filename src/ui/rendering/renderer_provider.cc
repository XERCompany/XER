#include "rendering/renderer_provider.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include "rendering/vulkan/vulkan_renderer.h"
#include "rendering/windows/direct2d_renderer.h"
#include "rendering/windows/software_renderer.h"
#endif

namespace XER::UI::Rendering {

namespace {

std::string NormalizeName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char ch : name) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            out.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return out;
}

#if defined(_WIN32)

class Direct2DProvider final : public RendererProvider {
public:
    RendererBackend Backend() const override { return RendererBackend::kD2D; }
    std::unique_ptr<Renderer> Create(void* native_window) override {
        return std::make_unique<Windows::Direct2DRenderer>(
            static_cast<HWND>(native_window));
    }
};

class VulkanProvider final : public RendererProvider {
public:
    RendererBackend Backend() const override {
        return RendererBackend::kVulkan;
    }
    std::unique_ptr<Renderer> Create(void* native_window) override {
        // VulkanRenderer::Create performs the whole probe (loader dll,
        // instance, WSI surface, device, swapchain) and returns nullptr
        // on any failure — no exceptions escape.
        return Vulkan::VulkanRenderer::Create(
            static_cast<HWND>(native_window), nullptr);
    }
};

class SoftwareProvider final : public RendererProvider {
public:
    RendererBackend Backend() const override {
        return RendererBackend::kSoftware;
    }
    std::unique_ptr<Renderer> Create(void* native_window) override {
        return std::make_unique<Windows::SoftwareRenderer>(
            static_cast<HWND>(native_window));
    }
};

// Tries one provider; nullptr result or a construction throw both count
// as "backend unavailable" and advance the fallback chain.
std::unique_ptr<Renderer> TryProvider(RendererProvider& provider,
                                      void* native_window) {
    try {
        return provider.Create(native_window);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "renderer provider %.*s failed: %s\n",
                     static_cast<int>(
                         RendererBackendName(provider.Backend()).size()),
                     RendererBackendName(provider.Backend()).data(),
                     e.what());
        return nullptr;
    } catch (...) {
        std::fprintf(stderr, "renderer provider %.*s failed\n",
                     static_cast<int>(
                         RendererBackendName(provider.Backend()).size()),
                     RendererBackendName(provider.Backend()).data());
        return nullptr;
    }
}

#endif // _WIN32

} // namespace

std::string_view RendererBackendName(RendererBackend backend) {
    switch (backend) {
        case RendererBackend::kAuto:
            return "auto";
        case RendererBackend::kSoftware:
            return "software";
        case RendererBackend::kD2D:
            return "d2d";
        case RendererBackend::kD3D11:
            return "d3d11";
        case RendererBackend::kVulkan:
            return "vulkan";
    }
    return "auto";
}

bool ParseRendererBackend(std::string_view name, RendererBackend* out) {
    const std::string normalized = NormalizeName(name);
    RendererBackend parsed = RendererBackend::kAuto;
    if (normalized == "auto") {
        parsed = RendererBackend::kAuto;
    } else if (normalized == "software" || normalized == "sw" ||
               normalized == "cpu") {
        parsed = RendererBackend::kSoftware;
    } else if (normalized == "d2d" || normalized == "direct2d") {
        parsed = RendererBackend::kD2D;
    } else if (normalized == "d3d11" || normalized == "direct3d11" ||
               normalized == "dx11") {
        parsed = RendererBackend::kD3D11;
    } else if (normalized == "vulkan" || normalized == "vk") {
        parsed = RendererBackend::kVulkan;
    } else {
        return false;
    }
    if (out != nullptr) {
        *out = parsed;
    }
    return true;
}

RendererBackend ResolveRendererBackend(const char* env_value) {
    if (env_value == nullptr) {
        return RendererBackend::kAuto;
    }
    RendererBackend parsed = RendererBackend::kAuto;
    if (!ParseRendererBackend(env_value, &parsed)) {
        if (env_value[0] != '\0') {
            std::fprintf(stderr,
                         "XER_UI_RENDERER: unknown value '%s' — using "
                         "auto (valid: vulkan|d2d|d3d11|software|auto)\n",
                         env_value);
        }
        return RendererBackend::kAuto;
    }
    return parsed;
}

RendererBackend ResolveRendererBackendFromEnv() {
    return ResolveRendererBackend(std::getenv("XER_UI_RENDERER"));
}

std::unique_ptr<Renderer> CreateRenderer(RendererBackend backend,
                                         void* native_window) {
#if defined(_WIN32)
    Direct2DProvider d2d;
    VulkanProvider vulkan;
    SoftwareProvider software;

    switch (backend) {
        case RendererBackend::kD2D:
        case RendererBackend::kD3D11:
            // kD3D11 selects the provider owning the D3D11 device layer —
            // Direct2DRenderer (see renderer_provider.h for the alias
            // rationale).
            if (auto r = TryProvider(d2d, native_window)) {
                return r;
            }
            break;
        case RendererBackend::kVulkan:
            if (auto r = TryProvider(vulkan, native_window)) {
                return r;
            }
            break;
        case RendererBackend::kSoftware:
            if (auto r = TryProvider(software, native_window)) {
                return r;
            }
            break;
        case RendererBackend::kAuto:
            break;
    }

    // Automatic / fallback chain: most complete first, always-available
    // last. An explicit-but-failed request continues down this chain.
    if (backend != RendererBackend::kD2D &&
        backend != RendererBackend::kD3D11) {
        if (auto r = TryProvider(d2d, native_window)) {
            return r;
        }
    }
    if (backend != RendererBackend::kVulkan) {
        if (auto r = TryProvider(vulkan, native_window)) {
            return r;
        }
    }
    if (backend != RendererBackend::kSoftware) {
        if (auto r = TryProvider(software, native_window)) {
            return r;
        }
    }
    return nullptr;
#else
    // No native windowing backends off Windows yet (Direct2D/Vulkan-WSI/
    // GDI software are HWND-based). A POSIX software provider lands with
    // the platform port.
    (void)backend;
    (void)native_window;
    return nullptr;
#endif
}

std::unique_ptr<Renderer> CreateRendererFromEnv(void* native_window) {
    return CreateRenderer(ResolveRendererBackendFromEnv(), native_window);
}

} // namespace XER::UI::Rendering
