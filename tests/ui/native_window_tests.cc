#include "test_harness.h"

#include <exception>
#include <memory>
#include <string>

#include <windows.h>

#include "core/window.h"
#include "platform/windows/input_translator.h"
#include "rendering/renderer.h"
#include "rendering/windows/direct2d_renderer.h"
#include "theme/tokens.h"
#include "widgets/widget.h"

namespace {

using XER::UI::Core::Window;
using XER::UI::Layout::Rect;
using XER::UI::Layout::Size;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::Windows::Direct2DRenderer;
using XER::UI::Theme::Tokens;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::Widget;
using XER::UI::Platform::Windows::InputTranslator;

class LayoutRecordingWidget final : public Widget {
public:
    void Render(Renderer& renderer, const Tokens& tokens) override {
        renderer.FillRect(GetBounds(), tokens.surface, 0.0f);
    }
    void Layout(const Rect& bounds) override { last_layout = bounds; }
    bool HandleEvent(const Event&) override { return false; }
    Size GetMinSize() const override { return {}; }
    Size GetPreferredSize() const override { return {}; }
    Size GetMaxSize() const override { return {}; }

    Rect last_layout;
};

} // namespace

XER_UI_TEST(TranslatesWin32KeyAndCharacterMessages) {
    InputTranslator translator;

    auto tab = translator.Translate(WM_KEYDOWN, VK_TAB, 0);
    XER_UI_EXPECT_TRUE(tab.has_value());
    XER_UI_EXPECT_EQ(Event::Type::KeyDown, tab->type);
    XER_UI_EXPECT_EQ(XER::UI::Widgets::KeyCode::Tab, tab->key);
    XER_UI_EXPECT_FALSE(tab->is_shift);

    XER_UI_EXPECT_FALSE(translator.Translate(WM_KEYDOWN, VK_SHIFT, 0).has_value());
    auto shift_tab = translator.Translate(WM_KEYDOWN, VK_TAB, 0);
    XER_UI_EXPECT_TRUE(shift_tab.has_value());
    XER_UI_EXPECT_EQ(XER::UI::Widgets::KeyCode::Tab, shift_tab->key);
    XER_UI_EXPECT_TRUE(shift_tab->is_shift);
    XER_UI_EXPECT_FALSE(translator.Translate(WM_KEYUP, VK_SHIFT, 0).has_value());

    auto enter = translator.Translate(WM_KEYDOWN, VK_RETURN, 0);
    XER_UI_EXPECT_TRUE(enter.has_value());
    XER_UI_EXPECT_EQ(XER::UI::Widgets::KeyCode::Enter, enter->key);

    auto backspace = translator.Translate(WM_KEYDOWN, VK_BACK, 0);
    XER_UI_EXPECT_TRUE(backspace.has_value());
    XER_UI_EXPECT_EQ(XER::UI::Widgets::KeyCode::Backspace, backspace->key);

    auto character = translator.Translate(WM_CHAR, L'x', 0);
    XER_UI_EXPECT_TRUE(character.has_value());
    XER_UI_EXPECT_EQ(Event::Type::TextInput, character->type);
    XER_UI_EXPECT_EQ(std::string("x"), character->text);
}

XER_UI_TEST(CombinesUtf16SurrogatePairIntoUtf8) {
    InputTranslator translator;

    XER_UI_EXPECT_FALSE(translator.Translate(WM_CHAR, 0xD83D, 0).has_value());
    const auto text = translator.Translate(WM_CHAR, 0xDE00, 0);
    XER_UI_EXPECT_TRUE(text.has_value());
    XER_UI_EXPECT_EQ(Event::Type::TextInput, text->type);
    XER_UI_EXPECT_EQ(std::string("\xF0\x9F\x98\x80"), text->text);
}

XER_UI_TEST(TranslatesMouseAndFocusMessages) {
    InputTranslator translator;

    const auto move = translator.Translate(WM_MOUSEMOVE, 0, MAKELPARAM(12, -7));
    XER_UI_EXPECT_TRUE(move.has_value());
    XER_UI_EXPECT_EQ(Event::Type::MouseMove, move->type);
    XER_UI_EXPECT_EQ(12.0f, move->position.x);
    XER_UI_EXPECT_EQ(-7.0f, move->position.y);

    const auto down = translator.Translate(WM_LBUTTONDOWN, 0, MAKELPARAM(4, 9));
    XER_UI_EXPECT_TRUE(down.has_value());
    XER_UI_EXPECT_EQ(Event::Type::MouseDown, down->type);
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Left, down->mouse_button);
    const auto right_down = translator.Translate(WM_RBUTTONDOWN, 0, MAKELPARAM(4, 9));
    XER_UI_EXPECT_TRUE(right_down.has_value());
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Right, right_down->mouse_button);
    const auto middle_down = translator.Translate(WM_MBUTTONDOWN, 0, MAKELPARAM(4, 9));
    XER_UI_EXPECT_TRUE(middle_down.has_value());
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Middle, middle_down->mouse_button);
    const auto up = translator.Translate(WM_LBUTTONUP, 0, MAKELPARAM(4, 9));
    XER_UI_EXPECT_TRUE(up.has_value());
    XER_UI_EXPECT_EQ(Event::Type::MouseUp, up->type);
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Left, up->mouse_button);
    const auto right_up = translator.Translate(WM_RBUTTONUP, 0, MAKELPARAM(4, 9));
    XER_UI_EXPECT_TRUE(right_up.has_value());
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Right, right_up->mouse_button);
    const auto middle_up = translator.Translate(WM_MBUTTONUP, 0, MAKELPARAM(4, 9));
    XER_UI_EXPECT_TRUE(middle_up.has_value());
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Middle, middle_up->mouse_button);

    const auto focus = translator.Translate(WM_SETFOCUS, 0, 0);
    XER_UI_EXPECT_TRUE(focus.has_value());
    XER_UI_EXPECT_EQ(Event::Type::Focus, focus->type);
    const auto blur = translator.Translate(WM_KILLFOCUS, 0, 0);
    XER_UI_EXPECT_TRUE(blur.has_value());
    XER_UI_EXPECT_EQ(Event::Type::Blur, blur->type);
}

XER_UI_TEST(RejectsInvalidDirect2DWindowTarget) {
    bool surfaced_error = false;
    try {
        Direct2DRenderer renderer(nullptr);
        renderer.BeginFrame(Rect(0.0f, 0.0f, 10.0f, 10.0f));
    } catch (const std::exception&) {
        surfaced_error = true;
    }
    XER_UI_EXPECT_TRUE(surfaced_error);
}

XER_UI_TEST(WindowReplacesContentRelayoutsAndPaintsWithoutContent) {
    Window window;
    auto first = std::make_shared<LayoutRecordingWidget>();
    auto replacement = std::make_shared<LayoutRecordingWidget>();

    window.SetContent(first);
    XER_UI_EXPECT_EQ(first, window.GetContent());
    window.SetContent(replacement);
    XER_UI_EXPECT_EQ(replacement, window.GetContent());
    XER_UI_EXPECT_TRUE(window.Create("XER UI Native Test", 320, 240));

    SendMessageW(window.GetHWND(), WM_SIZE, SIZE_RESTORED, MAKELPARAM(640, 480));
    const float dpi_scale = 96.0f / static_cast<float>(GetDpiForWindow(window.GetHWND()));
    XER_UI_EXPECT_EQ(640.0f * dpi_scale, replacement->last_layout.width);
    XER_UI_EXPECT_EQ(480.0f * dpi_scale, replacement->last_layout.height);
    XER_UI_EXPECT_EQ(0.0f, first->last_layout.width);

    window.SetContent(nullptr);
    window.SetTheme(XER::UI::Theme::Variant::Dark);
    SendMessageW(window.GetHWND(), WM_PAINT, 0, 0);
    XER_UI_EXPECT_FALSE(window.GetContent() != nullptr);
    XER_UI_EXPECT_TRUE(window.GetLastRenderError().empty());
}

XER_UI_TEST(WindowAppliesDpiChangeAndRelayoutsUsingNewDpi) {
    Window window;
    auto content = std::make_shared<LayoutRecordingWidget>();
    window.SetContent(content);
    XER_UI_EXPECT_TRUE(window.Create("XER UI DPI Test", 320, 240));
    SendMessageW(window.GetHWND(), WM_PAINT, 0, 0);
    XER_UI_EXPECT_TRUE(window.GetLastRenderError().empty());

    RECT suggested{100, 120, 700, 600};
    SendMessageW(
        window.GetHWND(),
        WM_DPICHANGED,
        MAKELONG(192, 192),
        reinterpret_cast<LPARAM>(&suggested)
    );

    RECT actual{};
    XER_UI_EXPECT_TRUE(GetWindowRect(window.GetHWND(), &actual));
    XER_UI_EXPECT_EQ(suggested.left, actual.left);
    XER_UI_EXPECT_EQ(suggested.top, actual.top);
    XER_UI_EXPECT_EQ(suggested.right, actual.right);
    XER_UI_EXPECT_EQ(suggested.bottom, actual.bottom);

    RECT client{};
    XER_UI_EXPECT_TRUE(GetClientRect(window.GetHWND(), &client));
    XER_UI_EXPECT_EQ(static_cast<float>(client.right) * 0.5f, content->last_layout.width);
    XER_UI_EXPECT_EQ(static_cast<float>(client.bottom) * 0.5f, content->last_layout.height);

    SendMessageW(window.GetHWND(), WM_PAINT, 0, 0);
    XER_UI_EXPECT_TRUE(window.GetLastRenderError().empty());
}
