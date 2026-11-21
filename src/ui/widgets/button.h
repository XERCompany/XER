#ifndef XER_UI_WIDGETS_BUTTON_H
#define XER_UI_WIDGETS_BUTTON_H

#include <functional>
#include <string>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

enum class ButtonStyle {
    Primary,
    Secondary,
    Outline,
    // shadcn "ghost": transparent until hover.
    Ghost,
    // shadcn "destructive": danger fill for irreversible actions.
    Destructive
};

class Button final : public Widget {
public:
    explicit Button(std::string text = {}, ButtonStyle style = ButtonStyle::Secondary);

    void SetText(std::string text);
    const std::string& GetText() const;
    std::string GetDebugText() const override { return text_; }
    void SetOnClick(std::function<void()> callback);
    void SetStyle(ButtonStyle style);

    // ---- optional icon ----
    // src/ui has no icon pack of its own, so icon lookup goes through a
    // resolver the host app installs once at boot (e.g. the video
    // editor wires it to its raster IconPack). Signature matches the
    // app's draw helper: draws `icon_name` rasterized at `px` tinted
    // `tint` centered inside `box`, returns false when unavailable.
    using IconResolver =
        std::function<bool(Rendering::Renderer& renderer,
                           const char* icon_name, int px,
                           Theme::Color tint, Layout::Rect box)>;
    static void SetIconResolver(IconResolver resolver);
    // Logical icon name drawn left of the label (centered when the
    // label is empty), ~16 DIP, tinted with the label color. Empty /
    // no resolver keeps the button text-only — full back-compat.
    void SetIcon(std::string icon_name);
    const std::string& GetIcon() const { return icon_; }
    // Busy state: draws a spinner inside the button, keeps its width and
    // swallows clicks until cleared.
    void SetLoading(bool loading);
    bool IsLoading() const { return loading_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void Activate();

    std::string text_;
    std::string icon_;
    std::function<void()> on_click_;
    ButtonStyle style_;
    bool loading_ = false;

    // Motion state (springs/tweens per the XER motion rules).
    Motion::Spring press_;       // 0..1 while the pointer is held down
    Motion::Tween hover_t_;      // eased hover wash
    float spinner_angle_ = 0.0f; // loading spinner rotation
    bool held_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_BUTTON_H
