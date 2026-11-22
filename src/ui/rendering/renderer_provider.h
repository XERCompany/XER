#ifndef XER_UI_RENDERING_RENDERER_PROVIDER_H
#define XER_UI_RENDERING_RENDERER_PROVIDER_H

#include <memory>
#include <string_view>

#include "rendering/renderer.h"

namespace XER::UI::Rendering {

// Selectable rendering providers for the UI surface.
//
//   kAuto     — pick the best provider available on this machine
//               (currently Direct2D/D3D11 first, then Vulkan, then the
//               software fallback — the order walks most-complete to
//               always-available, NOT raw GPU preference).
//   kSoftware — CPU rasterizer into a BGRA buffer, presented via GDI.
//               Always succeeds; also usable headless (null HWND) which
//               is how tests inspect pixels.
//   kD2D      — Direct2D/DirectWrite over a D3D11 device + DXGI
//               swapchain (the full-featured reference implementation).
//   kD3D11    — alias for the same GPU provider: Direct2DRenderer owns
//               the D3D11 device and swapchain, so "the D3D11 backend" IS
//               that device layer. A raw D3D11-only quad pipeline would
//               duplicate the same GPU work for no gain; the enum value
//               exists so callers can select the D3D11-device provider
//               explicitly.
//   kD2D2     — Direct2DEngine: same D2D/DWrite feature set, but the
//               swapchain lives on a dedicated present thread (second
//               D3D11 device + keyed-mutex shared canvas), so a Present
//               stall can never freeze the UI thread. Default in the
//               auto chain; "d2d" selects the legacy single-threaded
//               renderer for A/B.
//   kVulkan   — real Vulkan WSI renderer (dynamic vulkan-1.dll loader;
//               returns nullptr when no loader/ICD is present, which the
//               factory treats as "unavailable" and falls back).
enum class RendererBackend {
    kAuto,
    kSoftware,
    kD2D,
    kD3D11,
    kVulkan,
    kD2D2,
};

// Canonical lowercase names accepted by ParseRendererBackend and produced
// by RendererBackendName ("auto", "software", "d2d", "d3d11", "vulkan").
std::string_view RendererBackendName(RendererBackend backend);

// Parses a backend name (case-insensitive, surrounding whitespace ok).
// Returns false for empty/unknown names; `out` is left untouched then.
bool ParseRendererBackend(std::string_view name, RendererBackend* out);

// Maps a raw XER_UI_RENDERER value to a backend. nullptr, empty and
// unrecognized strings resolve to kAuto (the env var is a hint, never a
// hard error). Pure — takes the string so tests need no env mutation.
RendererBackend ResolveRendererBackend(const char* env_value);

// Reads XER_UI_RENDERER from the environment and resolves it.
RendererBackend ResolveRendererBackendFromEnv();

// Provider contract: one factory per backend. Providers that cannot
// initialize on this machine (e.g. no Vulkan loader/ICD) return nullptr
// from Create() instead of throwing.
class RendererProvider {
public:
    virtual ~RendererProvider() = default;
    virtual RendererBackend Backend() const = 0;
    // `native_window` is an HWND on Windows (pass nullptr for the
    // headless-capable software provider).
    virtual std::unique_ptr<Renderer> Create(void* native_window) = 0;
};

// Instantiates a renderer for `backend` bound to `native_window` (HWND).
// Explicit backends are tried first; on failure the factory walks the
// automatic preference chain (kD2D2 -> kD2D/kD3D11 -> kVulkan -> kSoftware) so the
// call only returns nullptr when even the software provider failed —
// effectively "always a renderer" on Windows. Never throws.
std::unique_ptr<Renderer> CreateRenderer(RendererBackend backend,
                                         void* native_window);

// CreateRenderer(ResolveRendererBackendFromEnv(), native_window).
// Precedence: XER_UI_RENDERER explicit value > kAuto chain. Documented
// env values: vulkan | d2d | d2d2 | d3d11 | software | auto (case-insensitive).
std::unique_ptr<Renderer> CreateRendererFromEnv(void* native_window);

} // namespace XER::UI::Rendering

#endif // XER_UI_RENDERING_RENDERER_PROVIDER_H
