#ifndef XER_UI_WIDGETS_EQ_EDITOR_H
#define XER_UI_WIDGETS_EQ_EDITOR_H

#include <array>
#include <functional>
#include <memory>

#include "widgets/widget.h"
#include "widgets/fx_rack_state.h"

namespace XER::UI::Widgets {

// Graphical EQ editor: log-frequency grid, dB grid, the combined magnitude
// response curve and five draggable band handles. Dragging a handle moves
// frequency (x) and gain (y); the mouse wheel over a handle adjusts Q;
// clicking a handle resets its gain/Q. All edits write through to the
// shared FxRackState and report via on_changed.
class EqEditorWidget final : public Widget {
public:
    using ResponseFn = std::function<float(float frequency_hz)>;

    EqEditorWidget(std::array<FxEqBandState, 5>* bands,
                   ResponseFn response,
                   std::function<void()> on_changed);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override { return {340.0f, 150.0f}; }
    Layout::Size GetPreferredSize() const override { return {600.0f, 200.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

private:
    static float FxToX(float frequency_hz, const Layout::Rect& plot);
    static float DbToY(float db, const Layout::Rect& plot);
    static float XToFreq(float x, const Layout::Rect& plot);
    static float YToDb(float y, const Layout::Rect& plot);
    Layout::Point HandlePos(int band, const Layout::Rect& plot) const;
    int HitHandle(const Layout::Point& position, const Layout::Rect& plot) const;
    Layout::Rect PlotRect() const;
    void ApplyHoverText(Rendering::Renderer& renderer, const Theme::Tokens& tokens,
                        const Layout::Rect& plot);

    std::array<FxEqBandState, 5>* bands_ = nullptr;
    ResponseFn response_;
    std::function<void()> on_changed_;

    int dragging_band_ = -1;
    int hover_band_ = -1;
    bool just_dragged_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_EQ_EDITOR_H
