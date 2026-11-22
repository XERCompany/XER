// Tests for the universal widget layout-geometry contract on
// src/ui/widgets/widget.h — margin/indent/alignment/fixed-size
// resolution inside SetBounds, Qt-style aliases, and the Surface
// custom-paint widget.

#include "ui/layout/flex.h"
#include "ui/theme/theme.h"
#include "ui/widgets/label.h"
#include "ui/widgets/surface.h"
#include "ui/widgets/widget.h"
#include "ui/widgets/widget_tree.h"

#include <memory>

#include "test_harness.h"

namespace {

using XER::UI::Layout::AlignedRect;
using XER::UI::Layout::AlignMode;
using XER::UI::Layout::EdgeInsets;
using XER::UI::Layout::Insets;
using XER::UI::Layout::Rect;
using XER::UI::Layout::Size;
using XER::UI::Widgets::Alignment;
using XER::UI::Widgets::Label;
using XER::UI::Widgets::Surface;
using XER::UI::Widgets::TextDirection;
using XER::UI::Widgets::Widget;
using XER::UI::Widgets::WidgetTree;

bool NearlyEqual(float a, float b, float eps = 0.001f) {
    return (a - b) < eps && (b - a) < eps;
}

bool RectEquals(const Rect& r, float x, float y, float w, float h) {
    return NearlyEqual(r.x, x) && NearlyEqual(r.y, y) &&
           NearlyEqual(r.width, w) && NearlyEqual(r.height, h);
}

} // namespace

XER_UI_TEST(WidgetDefaultsAdoptTheSlotVerbatim) {
    Label label;
    label.SetText("hello");
    label.Layout(Rect{10, 10, 300, 50});
    XER_UI_EXPECT_TRUE(RectEquals(label.GetBounds(), 10, 10, 300, 50));
    XER_UI_EXPECT_EQ(0, label.indent());
    XER_UI_EXPECT_TRUE(label.margin().IsEmpty());
    XER_UI_EXPECT_EQ(Alignment::Default, label.alignment());
    XER_UI_EXPECT_FALSE(label.hasScaledContents());
    XER_UI_EXPECT_FALSE(label.HasFixedWidth());
    XER_UI_EXPECT_FALSE(label.HasFixedHeight());
}

XER_UI_TEST(WidgetMarginShrinksAdoptedBounds) {
    Label label;
    label.SetMargin(Insets{4.0f, 2.0f, 6.0f, 8.0f});
    label.Layout(Rect{0, 0, 100, 50});
    XER_UI_EXPECT_TRUE(RectEquals(label.GetBounds(), 4, 2, 90, 40));
}

XER_UI_TEST(WidgetIndentAddsLeadingInset) {
    Label label;
    label.SetIndent(12);
    label.Layout(Rect{0, 0, 100, 40});
    XER_UI_EXPECT_TRUE(RectEquals(label.GetBounds(), 12, 0, 88, 40));

    // RTL mirrors the indent to the right edge.
    Label rtl;
    rtl.SetIndent(12);
    rtl.SetTextDirection(TextDirection::Rtl);
    rtl.Layout(Rect{0, 0, 100, 40});
    XER_UI_EXPECT_TRUE(RectEquals(rtl.GetBounds(), 0, 0, 88, 40));
}

XER_UI_TEST(WidgetAlignmentPositionsPreferredSize) {
    Label label;
    label.SetText("Ag");  // preferred ~14x14 via TextStyle-free path
    const Size pref = label.GetPreferredSize();
    label.SetAlignment(Alignment::HCenter | Alignment::Bottom);
    label.Layout(Rect{0, 0, 200, 100});
    const Rect b = label.GetBounds();
    XER_UI_EXPECT_TRUE(NearlyEqual(b.width, pref.width));
    XER_UI_EXPECT_TRUE(NearlyEqual(b.height, pref.height));
    XER_UI_EXPECT_TRUE(NearlyEqual(b.x, (200.0f - pref.width) * 0.5f));
    XER_UI_EXPECT_TRUE(NearlyEqual(b.y, 100.0f - pref.height));
}

XER_UI_TEST(WidgetAlignmentTrailingMirrorsInRtl) {
    Label label;
    label.SetText("Ag");
    const Size pref = label.GetPreferredSize();
    label.SetAlignment(Alignment::Trailing | Alignment::Top);
    label.Layout(Rect{0, 0, 200, 100});
    XER_UI_EXPECT_TRUE(
        NearlyEqual(label.GetBounds().x, 200.0f - pref.width));

    Label rtl;
    rtl.SetText("Ag");
    rtl.SetTextDirection(TextDirection::Rtl);
    rtl.SetAlignment(Alignment::Trailing | Alignment::Top);
    rtl.Layout(Rect{0, 0, 200, 100});
    XER_UI_EXPECT_TRUE(NearlyEqual(rtl.GetBounds().x, 0.0f));
}

XER_UI_TEST(WidgetFixedSizeOverridesPreferred) {
    Label label;
    label.SetText("a longer piece of text");
    label.SetFixedSize(Size{64.0f, 20.0f});
    // Default alignment: fixed extents pack at the cell start (Qt).
    label.Layout(Rect{0, 0, 300, 80});
    XER_UI_EXPECT_TRUE(RectEquals(label.GetBounds(), 0, 0, 64, 20));

    // Fixed extent is honored even under explicit Fill.
    label.SetAlignment(Alignment::Fill);
    label.Layout(Rect{0, 0, 300, 80});
    XER_UI_EXPECT_TRUE(RectEquals(label.GetBounds(), 0, 0, 64, 20));

    label.ClearFixedSize();
    XER_UI_EXPECT_FALSE(label.HasFixedWidth());
    label.Layout(Rect{0, 0, 300, 80});
    XER_UI_EXPECT_TRUE(RectEquals(label.GetBounds(), 0, 0, 300, 80));
}

XER_UI_TEST(WidgetQtAliasesAndMoveResize) {
    Label label;
    label.SetText("Ag");
    label.Layout(Rect{5, 6, 70, 30});
    XER_UI_EXPECT_TRUE(RectEquals(label.rect(), 5, 6, 70, 30));
    XER_UI_EXPECT_EQ(5.0f, label.x());
    XER_UI_EXPECT_EQ(6.0f, label.y());
    XER_UI_EXPECT_EQ(70.0f, label.width());
    XER_UI_EXPECT_EQ(30.0f, label.height());
    XER_UI_EXPECT_TRUE(label.sizeHint() == label.GetPreferredSize());
    XER_UI_EXPECT_TRUE(label.minimumSizeHint() == label.GetMinSize());

    label.Move(20, 24);
    XER_UI_EXPECT_TRUE(RectEquals(label.GetBounds(), 20, 24, 70, 30));
    label.Resize(50, 12);
    XER_UI_EXPECT_TRUE(RectEquals(label.GetBounds(), 20, 24, 50, 12));
    label.Move(Rect{1, 2, 3, 4});
    XER_UI_EXPECT_TRUE(RectEquals(label.GetBounds(), 1, 2, 3, 4));
}

XER_UI_TEST(WidgetMarginPlusAlignmentCompose) {
    Label label;
    label.SetText("Ag");
    const Size pref = label.GetPreferredSize();
    label.SetMargin(Insets{10.0f, 10.0f, 10.0f, 10.0f});
    label.SetAlignment(Alignment::Center);
    label.Layout(Rect{0, 0, 200, 100});
    // Box = {10,10,180,80}; preferred centered inside it.
    const Rect b = label.GetBounds();
    XER_UI_EXPECT_TRUE(
        NearlyEqual(b.x, 10.0f + (180.0f - pref.width) * 0.5f));
    XER_UI_EXPECT_TRUE(
        NearlyEqual(b.y, 10.0f + (80.0f - pref.height) * 0.5f));
}

XER_UI_TEST(AlignedRectPositionsWithinCell) {
    const Rect cell{10, 20, 100, 50};
    const Size size{40, 20};
    const Rect lead = AlignedRect(cell, size, AlignMode::Leading,
                                  AlignMode::Leading);
    XER_UI_EXPECT_TRUE(RectEquals(lead, 10, 20, 40, 20));
    const Rect center = AlignedRect(cell, size, AlignMode::Center,
                                    AlignMode::Center);
    XER_UI_EXPECT_TRUE(RectEquals(center, 40, 35, 40, 20));
    const Rect trail = AlignedRect(cell, size, AlignMode::Trailing,
                                   AlignMode::Trailing);
    XER_UI_EXPECT_TRUE(RectEquals(trail, 70, 50, 40, 20));
    const Rect fill = AlignedRect(cell, size, AlignMode::Fill,
                                  AlignMode::Fill);
    XER_UI_EXPECT_TRUE(RectEquals(fill, 10, 20, 100, 50));
    // Oversized content clamps to the cell.
    const Rect clamped = AlignedRect(cell, Size{500, 500},
                                     AlignMode::Leading,
                                     AlignMode::Leading);
    XER_UI_EXPECT_TRUE(RectEquals(clamped, 10, 20, 100, 50));
}

XER_UI_TEST(SurfaceHostsPaintAndEventCallbacks) {
    auto surface = std::make_shared<Surface>();
    surface->SetContentSize(Size{16.0f, 9.0f});
    surface->Layout(Rect{0, 0, 200, 200});
    // Letterboxed: 16:9 content in a square surface → pillarboxed.
    const Rect content = surface->ContentRect();
    XER_UI_EXPECT_TRUE(NearlyEqual(content.width, 200.0f));
    XER_UI_EXPECT_TRUE(NearlyEqual(content.height, 112.5f));
    XER_UI_EXPECT_TRUE(NearlyEqual(content.y, 43.75f));

    surface->SetScaledContents(true);
    XER_UI_EXPECT_TRUE(RectEquals(surface->ContentRect(), 0, 0, 200, 200));
    surface->SetScaledContents(false);

    bool painted = false;
    Rect painted_rect;
    surface->SetPaintCallback(
        [&](XER::UI::Rendering::Renderer&, const Rect& rect,
            const XER::UI::Theme::Tokens&) {
            painted = true;
            painted_rect = rect;
        });
    XER::UI::Theme::Provider provider;
    // RecordingRenderer-free: Render only needs PushClip/PopClip,
    // which the base-class no-ops provide.
    class NullRenderer final : public XER::UI::Rendering::Renderer {
    public:
        void BeginFrame(Rect) override {}
        void EndFrame() override {}
        void FillRect(Rect, XER::UI::Theme::Color, float) override {}
        void StrokeRect(Rect, XER::UI::Theme::Color, float,
                        float) override {}
        void DrawLine(XER::UI::Layout::Point, XER::UI::Layout::Point,
                      XER::UI::Theme::Color, float) override {}
        void DrawText(std::string_view, Rect,
                      XER::UI::Rendering::TextStyle) override {}
        Size MeasureText(std::string_view,
                         XER::UI::Rendering::TextStyle s) const override {
            return {0.0f, s.size};
        }
    } renderer;
    surface->Render(renderer, provider.GetTokens());
    XER_UI_EXPECT_TRUE(painted);
    XER_UI_EXPECT_TRUE(RectEquals(painted_rect, 0, 43.75f, 200, 112.5f));

    bool got_event = false;
    surface->SetEventCallback(
        [&](const XER::UI::Widgets::Event&) { got_event = true; return true; });
    XER_UI_EXPECT_TRUE(surface->IsFocusable());
    XER::UI::Widgets::Event ev;
    ev.type = XER::UI::Widgets::Event::Type::Click;
    ev.mouse_button = XER::UI::Widgets::MouseButton::Left;
    XER_UI_EXPECT_TRUE(surface->HandleEvent(ev));
    XER_UI_EXPECT_TRUE(got_event);

    // Dynamic preferred size callback wins over the stored default.
    surface->SetPreferredSizeCallback([] { return Size{320.0f, 180.0f}; });
    XER_UI_EXPECT_EQ(320.0f, surface->GetPreferredSize().width);
    XER_UI_EXPECT_EQ(180.0f, surface->sizeHint().height);
}

XER_UI_TEST(WidgetTreeLayoutHonorsRootBoxModel) {
    auto root = std::make_shared<Surface>();
    root->SetMargin(Insets{8.0f});
    root->SetAlignment(Alignment::Trailing | Alignment::Top);

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout(Rect{0, 0, 400, 300});
    // Slot {0,0,400,300} minus margin {8} → box {8,8,384,284};
    // preferred-size packed to the trailing/top edge.
    const Size pref = root->GetPreferredSize();
    XER_UI_EXPECT_TRUE(RectEquals(
        root->GetBounds(),
        8.0f + 384.0f - pref.width, 8.0f, pref.width, pref.height));
}
