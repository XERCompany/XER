#ifndef XER_UI_WIDGETS_INPUT_OTP_H
#define XER_UI_WIDGETS_INPUT_OTP_H

#include <functional>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// InputOTP — segmented one-time-code input: `length` cells, one character
// each. Typing fills cells left-to-right; Backspace deletes; paste of a
// full code distributes across cells. Fires on_complete when the code is
// full. Only digits/letters are accepted (no spaces).
class InputOTP final : public Widget {
public:
    explicit InputOTP(int length = 6);

    void SetLength(int length);
    int GetLength() const { return length_; }
    void SetCode(std::string code);
    std::string GetCode() const;
    void SetOnComplete(std::function<void(std::string)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void InputChar(char c);
    void Backspace();
    bool IsComplete() const;
    void MaybeComplete();
    Layout::Rect CellRect(int index, const Layout::Rect& bounds) const;

    int length_ = 6;
    std::string code_;
    int focused_cell_ = 0;
    std::function<void(std::string)> on_complete_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_INPUT_OTP_H
