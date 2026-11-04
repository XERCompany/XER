#ifndef XER_UI_WIDGETS_TEXT_INPUT_H
#define XER_UI_WIDGETS_TEXT_INPUT_H

#include <functional>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class TextInput final : public Widget {
public:
    explicit TextInput(std::string value = {});

    const std::string& GetValue() const;
    void SetValue(std::string value);
    void SetOnValueChanged(std::function<void(const std::string&)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void InsertText(const std::string& text);
    void NotifyValueChanged();

    std::string value_;
    std::size_t caret_ = 0;
    std::function<void(const std::string&)> on_value_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TEXT_INPUT_H
