#ifndef XER_UI_RENDERING_VULKAN_VULKAN_RENDERER_H
#define XER_UI_RENDERING_VULKAN_VULKAN_RENDERER_H

#include <windows.h>

#ifdef DrawText
#undef DrawText
#endif

#include <memory>
#include <string>

#include "rendering/renderer.h"

namespace XER::UI::Rendering::Vulkan {

// Real Vulkan backend: WSI swapchain on the given HWND, command buffers
// recorded per frame, colored/textured quads for rects and BGRA uploads.
// The API is loaded dynamically (LoadLibrary("vulkan-1.dll") +
// vkGetInstanceProcAddr), so no Vulkan SDK is needed to build or run —
// only a Vulkan loader + ICD (driver) on the machine. When the runtime
// is missing or any init step fails, Create() returns nullptr and the
// renderer-provider factory falls back to Direct2D/software.
//
// Implemented vs deferred (see also renderer.h defaults):
//   Implemented: BeginFrame/EndFrame (acquire, record, submit, present),
//     FillRect (sharp corners; `radius` is ignored — rounded geometry is
//     deferred to an SDF pipeline), StrokeRect, DrawLine, DrawImage
//     (WIC decode -> texture, corner_radius ignored), DrawFrameBgra /
//     DrawResourcePixels / DrawVideoPixels (BGRA -> VkImage uploads via
//     staging buffers), DrawSpectrumPixels (base-class per-bar rects),
//     DrawText/MeasureText (GDI coverage mask -> texture), PushClip/
//     PopClip (dynamic scissor), PushLayer/PopLayer (opacity multiplier),
//     SetTransform/ResetTransform (2x3 affine folded into NDC quads),
//     SetDpi/SetClearColor/SetFrameDamage/FreeResource.
//   Deferred: rounded FillRect corners, image corner_radius clipping,
//     blend modes & layer blur (falls back to plain opacity like the
//     Direct2D backend), DrawBackdropBlur (translucent-scrim
//     approximation), non-axis-aligned texture sampling (rotated
//     images/text draw on the transformed quad's AABB).
//   Retained-mode note: the swapchain is re-rendered every frame, so
//   TakeTargetsRecreated() always reports fresh targets — this makes
//   WidgetTree ignore damage culling and repaint fully each frame.
class VulkanRenderer final : public Renderer {
public:
    // Full probe + init. Returns nullptr (and fills error_out when
    // non-null) on ANY failure: no vulkan-1.dll, no usable ICD/GPU,
    // WSI surface or swapchain failure. Never throws.
    static std::unique_ptr<VulkanRenderer> Create(HWND hwnd,
                                                  std::string* error_out);

    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void SetDpi(unsigned dpi) override;
    void SetClearColor(Theme::Color color) override;
    void BeginFrame(Layout::Rect bounds) override;
    void EndFrame() override;
    bool TakeTargetsRecreated() override { return true; }
    void FillRect(Layout::Rect rect, Theme::Color color,
                  float radius) override;
    void StrokeRect(Layout::Rect rect, Theme::Color color, float width,
                    float radius) override;
    void DrawLine(Layout::Point start, Layout::Point end,
                  Theme::Color color, float width) override;
    void DrawText(std::string_view text, Layout::Rect rect,
                  TextStyle style) override;
    Layout::Size MeasureText(std::string_view text,
                             TextStyle style) const override;
    void PushClip(Layout::Rect rect) override;
    void PopClip() override;
    void DrawBackdropBlur(Layout::Rect rect, float radius) override;
    bool DrawImage(std::string_view path, Layout::Rect rect,
                   float opacity, float corner_radius) override;
    bool DrawFrameBgra(const void* bgra, int width, int height,
                       Layout::Rect dst_rect, float opacity) override;
    void PushLayer(float opacity, std::uint32_t blend_mode,
                   float blur) override;
    void PopLayer() override;
    void SetTransform(float a, float b, float c, float d, float tx,
                      float ty) override;
    void ResetTransform() override;
    bool DrawResourcePixels(const void* bgra, std::uint32_t id,
                            int width, int height, Layout::Rect dst,
                            float opacity, float corner_radius) override;
    bool DrawVideoPixels(const void* bgra, std::uint32_t id,
                         int width, int height, Layout::Rect dst,
                         float opacity, float corner_radius) override;
    void FreeResource(std::uint32_t id) override;

    // Implementation details — public only so the .cc's free helpers can
    // name the type (private nested types are not accessible outside the
    // class). Not part of the API contract.
    struct Impl;
    std::unique_ptr<Impl> impl_;

private:
    explicit VulkanRenderer(HWND hwnd);
};

} // namespace XER::UI::Rendering::Vulkan

#endif // XER_UI_RENDERING_VULKAN_VULKAN_RENDERER_H
