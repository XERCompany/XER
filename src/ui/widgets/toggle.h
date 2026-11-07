#ifndef XER_UI_WIDGETS_TOGGLE_H
#define XER_UI_WIDGETS_TOGGLE_H

#include <functional>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style toggle button: a pressed/unpressed button used for states
// like bold/italic or on/off flags.
class Toggle final : public Widget {
public:
    Toggle(std::string text = {}, bool pressed = false);

    void SetText(std::string text);
    const std::string& GetText() const { return text_; }
    void SetPressed(bool pressed);
    bool IsPressed() const { return pressed_; }
    void SetOnPressedChanged(std::function<void(bool)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string text_;
    bool pressed_ = false;
    std::function<void(bool)> on_pressed_changed_;
};

// shadcn-style toggle group: a row of toggle buttons where either a single
// option (exclusive) or multiple options may be active.
class ToggleGroup final : public Widget {
public:
    void SetOptions(std::vector<std::string> options, bool allow_multiple = false);
    void SetSelectedIndices(std::vector<int> indices);
    const std::vector<int>& GetSelectedIndices() const { return selected_; }
    bool IsSelected(int index) const;
    void SetOnChanged(std::function<void(const std::vector<int>&)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    int HitOption(const Layout::Point& position) const;
    void ToggleIndex(int index);

    std::vector<std::string> options_;
    std::vector<Layout::Rect> option_rects_;
    std::vector<int> selected_;
    bool allow_multiple_ = false;
    std::function<void(const std::vector<int>&)> on_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TOGGLE_H
