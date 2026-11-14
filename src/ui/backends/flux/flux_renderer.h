#ifndef XER_UI_BACKENDS_FLUX_FLUX_RENDERER_H
#define XER_UI_BACKENDS_FLUX_FLUX_RENDERER_H

#include <optional>
#include <string_view>
#include <vector>

#include "flux/rendering/render_context.h"
#include "rendering/renderer.h"

namespace XER::UI::Backends {

class FluxRenderer final : public Rendering::Renderer {
public:
    explicit FluxRenderer(XER::Flux::Rendering::RenderContext& context);

    void BeginFrame(Layout::Rect bounds) override;
    void EndFrame() override;
    void FillRect(Layout::Rect rect, Theme::Color color, float radius) override;
    void StrokeRect(Layout::Rect rect, Theme::Color color, float width, float radius) override;
    void DrawLine(
        Layout::Point start,
        Layout::Point end,
        Theme::Color color,
        float width
    ) override;
    void DrawText(std::string_view text, Layout::Rect rect, Rendering::TextStyle style) override;
    Layout::Size MeasureText(std::string_view text, Rendering::TextStyle style) const override;
    void PushClip(Layout::Rect rect) override;
    void PopClip() override;

private:
    XER::Flux::Rendering::RenderContext& context_;
    // Stack of clip rectangles in cell coordinates (flux maps 1 unit = 1 cell).
    std::vector<Layout::Rect> clip_stack_;
    // Intersection of the whole clip stack, or an empty optional when unclipped.
    std::optional<Layout::Rect> EffectiveClip() const;
    Layout::Rect IntersectClip(Layout::Rect rect) const;
};

}  // namespace XER::UI::Backends

#endif  // XER_UI_BACKENDS_FLUX_FLUX_RENDERER_H
