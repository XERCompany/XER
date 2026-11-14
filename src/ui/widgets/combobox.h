#ifndef XER_UI_WIDGETS_COMBOBOX_H
#define XER_UI_WIDGETS_COMBOBOX_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Combobox — text input with a filtered dropdown of options. Typing
// filters the list; Enter/click selects; Esc closes.
class Combobox final : public Widget {
public:
    struct Option {
        std::string label;
        std::string value;  // empty → label is the value
    };

    static std::shared_ptr<Combobox> Create();

    void SetOptions(std::vector<Option> options);
    void SetValue(std::string value);
    std::string GetValue() const;
    std::string GetText() const { return text_; }
    void SetPlaceholder(std::string placeholder);
    void SetOnChange(std::function<void(const std::string&)> cb);
    void SetOnSelect(std::function<void(const Option&)> cb);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Combobox();
    std::vector<const Option*> Filtered() const;
    void UpdateFilter();
    void CloseDropdown();
    void SelectIndex(int idx);

    std::vector<Option> options_;
    std::vector<int> filtered_;   // indices into options_
    std::string text_;
    std::string value_;
    std::string placeholder_ = "Select…";
    bool open_ = false;
    int highlight_ = -1;
    float scroll_ = 0.0f;
    Layout::Rect dropdown_rect_{};
    std::function<void(const std::string&)> on_change_;
    std::function<void(const Option&)> on_select_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_COMBOBOX_H
