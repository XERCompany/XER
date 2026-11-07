#ifndef XER_UI_WIDGETS_RADIO_GROUP_H
#define XER_UI_WIDGETS_RADIO_GROUP_H

#include <functional>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style radio group: a vertical list of options with a single
// selection, drawn as native-looking radio indicators.
class RadioGroup final : public Widget {
public:
    void SetOptions(std::vector<std::string> options);
    void SetSelectedIndex(int index);
    int GetSelectedIndex() const { return selected_index_; }
    void SetOnChanged(std::function<void(int)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    int HitOption(const Layout::Point& position) const;

    std::vector<std::string> options_;
    std::vector<Layout::Rect> option_rects_;
    int selected_index_ = -1;
    int hovered_option_ = -1;
    std::function<void(int)> on_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_RADIO_GROUP_H
