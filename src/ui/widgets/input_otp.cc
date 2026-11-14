#include "input_otp.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

bool IsOtpChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

} // namespace

InputOTP::InputOTP(int length) : length_(std::max(1, length)) {
    SetFocusable(true);
}

void InputOTP::SetLength(int length) {
    length_ = std::max(1, length);
    if (static_cast<int>(code_.size()) > length_) {
        code_.resize(static_cast<std::size_t>(length_));
    }
    focused_cell_ = std::min(focused_cell_, length_ - 1);
    RequestLayout();
    Invalidate();
}

void InputOTP::SetCode(std::string code) {
    code_.clear();
    for (char c : code) {
        if (IsOtpChar(c) && static_cast<int>(code_.size()) < length_) {
            code_ += static_cast<char>(std::toupper(
                static_cast<unsigned char>(c)));
        }
    }
    focused_cell_ = static_cast<int>(code_.size());
    Invalidate();
}

std::string InputOTP::GetCode() const {
    return code_;
}

void InputOTP::SetOnComplete(std::function<void(std::string)> callback) {
    on_complete_ = std::move(callback);
}

void InputOTP::InputChar(char c) {
    if (!IsOtpChar(c)) return;
    if (static_cast<int>(code_.size()) >= length_) return;
    code_ += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    focused_cell_ = static_cast<int>(code_.size());
    Invalidate();
    MaybeComplete();
}

void InputOTP::Backspace() {
    if (code_.empty()) return;
    code_.pop_back();
    focused_cell_ = static_cast<int>(code_.size());
    Invalidate();
}

bool InputOTP::IsComplete() const {
    return static_cast<int>(code_.size()) == length_;
}

void InputOTP::MaybeComplete() {
    if (IsComplete() && on_complete_) {
        on_complete_(code_);
    }
}

Layout::Rect InputOTP::CellRect(int index, const Layout::Rect& bounds) const {
    constexpr float kCell = 44.0f;
    constexpr float kGap = 8.0f;
    const float total_w =
        static_cast<float>(length_) * kCell +
        static_cast<float>(length_ - 1) * kGap;
    float x = bounds.x + (bounds.width - total_w) * 0.5f;
    Layout::Rect r;
    r.x = x + static_cast<float>(index) * (kCell + kGap);
    r.y = bounds.y + (bounds.height - kCell) * 0.5f;
    r.width = kCell;
    r.height = kCell;
    return r;
}

void InputOTP::Render(Rendering::Renderer& renderer,
                      const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    const Rendering::TextStyle style{tokens.foreground, tokens.font_title, 600};
    for (int i = 0; i < length_; ++i) {
        const auto cell = CellRect(i, bounds);
        const bool active = IsFocused() && i == focused_cell_;
        const Theme::Color border =
            active ? tokens.accent : tokens.border;
        const Theme::Color fill =
            active ? tokens.surface : tokens.background;
        renderer.FillRect(cell, fill, tokens.radius_medium);
        renderer.StrokeRect(cell, border, active ? 2.0f : 1.0f,
                            tokens.radius_medium);

        if (i < static_cast<int>(code_.size())) {
            const std::string ch(1, code_[static_cast<std::size_t>(i)]);
            const auto ts = renderer.MeasureText(ch, style);
            Layout::Rect tr;
            tr.x = cell.x + (cell.width - ts.width) * 0.5f;
            tr.y = cell.y + (cell.height - ts.height) * 0.5f;
            tr.width = ts.width;
            tr.height = ts.height;
            renderer.DrawText(ch, tr, style);
        } else if (active) {
            // Caret hint: thin vertical line in the active empty cell.
            renderer.DrawLine(
                {cell.x + cell.width * 0.5f, cell.y + cell.height * 0.25f},
                {cell.x + cell.width * 0.5f, cell.y + cell.height * 0.75f},
                tokens.accent, 1.5f);
        }
    }
    RenderFocusRing(renderer, tokens, tokens.radius_medium);
}

void InputOTP::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool InputOTP::HandleEvent(const Event& event) {
    if (!IsEnabled()) return false;
    switch (event.type) {
        case Event::Type::TextInput:
            // TextInput carries typed characters (possibly multi-char paste).
            for (char c : event.text) {
                if (static_cast<int>(code_.size()) >= length_) break;
                InputChar(c);
            }
            return true;
        case Event::Type::KeyDown:
            if (event.key == KeyCode::Backspace) {
                Backspace();
                return true;
            }
            if (event.key == KeyCode::ArrowLeft) {
                focused_cell_ = std::max(0, focused_cell_ - 1);
                Invalidate();
                return true;
            }
            if (event.key == KeyCode::ArrowRight) {
                focused_cell_ = std::min(length_, focused_cell_ + 1);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::Focus:
            focused_cell_ = static_cast<int>(code_.size());
            Invalidate();
            return false;
        case Event::Type::Blur:
            Invalidate();
            return false;
        default:
            return false;
    }
}

Layout::Size InputOTP::GetMinSize() const {
    return {static_cast<float>(length_) * 44.0f +
                static_cast<float>(length_ - 1) * 8.0f,
            44.0f};
}

Layout::Size InputOTP::GetPreferredSize() const {
    return GetMinSize();
}

Layout::Size InputOTP::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
