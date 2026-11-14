// Tests for src/ui/layout/flex.h — pixel snapping, centering helpers, and
// overlap-free row distribution used by IDE-style widgets.

#include "ui/layout/flex.h"

#include <cmath>

#include "test_harness.h"

namespace {

using XER::UI::Layout::FlexAlign;
using XER::UI::Layout::Rect;

bool RectsOverlap(const Rect& a, const Rect& b) {
    return a.x < b.Right() && b.x < a.Right() &&
           a.y < b.Bottom() && b.y < a.Bottom();
}

bool InsideContainer(const Rect& item, const Rect& container, float eps = 0.51f) {
    return item.x >= container.x - eps &&
           item.Right() <= container.Right() + eps &&
           item.y >= container.y - eps &&
           item.Bottom() <= container.Bottom() + eps;
}

} // namespace

XER_UI_TEST(FlexSnapRoundsToPixelGrid) {
    XER_UI_EXPECT_EQ(3.0f, XER::UI::Layout::Snap(3.04f));
    XER_UI_EXPECT_EQ(3.0f, XER::UI::Layout::Snap(3.49f));
    XER_UI_EXPECT_EQ(4.0f, XER::UI::Layout::Snap(3.5f));
    XER_UI_EXPECT_EQ(1.5f, XER::UI::Layout::Snap(1.5f, 2.0f));
    XER_UI_EXPECT_EQ(2.0f, XER::UI::Layout::Snap(1.8f, 2.0f));
    XER_UI_EXPECT_EQ(1.5f, XER::UI::Layout::Snap(1.6f, 2.0f));
}

XER_UI_TEST(FlexSnapRectKeepsEdgesInside) {
    const Rect r{10.2f, 20.6f, 100.3f, 40.4f};
    const Rect s = XER::UI::Layout::SnapRect(r);
    XER_UI_EXPECT_TRUE(s.x == std::round(r.x));
    XER_UI_EXPECT_TRUE(s.Right() == std::round(r.Right()));
    XER_UI_EXPECT_TRUE(s.width >= 0.0f);
}

XER_UI_TEST(FlexCenteredRectCentersBothAxes) {
    const Rect container{0.0f, 0.0f, 200.0f, 100.0f};
    const Rect r = XER::UI::Layout::CenteredRect(
        container, XER::UI::Layout::Size{50.0f, 20.0f});
    XER_UI_EXPECT_EQ(75.0f, r.x);
    XER_UI_EXPECT_EQ(40.0f, r.y);
    XER_UI_EXPECT_EQ(50.0f, r.width);
    XER_UI_EXPECT_EQ(20.0f, r.height);
}

XER_UI_TEST(FlexCenteredRectHonorsPaddingAndClamps) {
    const Rect container{10.0f, 10.0f, 100.0f, 50.0f};
    const Rect r = XER::UI::Layout::CenteredRect(
        container, XER::UI::Layout::Size{500.0f, 500.0f}, 5.0f, 5.0f);
    XER_UI_EXPECT_EQ(90.0f, r.width);   // 100 - 2*5
    XER_UI_EXPECT_EQ(40.0f, r.height);  // 50 - 2*5
    XER_UI_EXPECT_TRUE(InsideContainer(r, container));
}

XER_UI_TEST(FlexVerticallyCenteredAlignsMiddle) {
    const Rect container{0.0f, 0.0f, 100.0f, 40.0f};
    const Rect r = XER::UI::Layout::VerticallyCentered(container, 16.0f);
    XER_UI_EXPECT_EQ(12.0f, r.y);
    XER_UI_EXPECT_EQ(16.0f, r.height);
    XER_UI_EXPECT_EQ(100.0f, r.width);
}

XER_UI_TEST(FlexDistributeRowEmptyInput) {
    const Rect c{0.0f, 0.0f, 200.0f, 30.0f};
    XER_UI_EXPECT_TRUE(XER::UI::Layout::DistributeRow(c, {}).empty());
    XER_UI_EXPECT_TRUE(XER::UI::Layout::DistributeRow(
        Rect{0, 0, 0, 30}, {10.0f}).empty());
}

XER_UI_TEST(FlexDistributeRowStartNoOverlapWide) {
    const Rect c{0.0f, 0.0f, 400.0f, 30.0f};
    const auto rects = XER::UI::Layout::DistributeRow(
        c, {80.0f, 120.0f, 60.0f}, {}, 8.0f, FlexAlign::Start);
    XER_UI_EXPECT_EQ(3u, rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i) {
        XER_UI_EXPECT_TRUE(InsideContainer(rects[i], c));
        if (i > 0) XER_UI_EXPECT_FALSE(RectsOverlap(rects[i - 1], rects[i]));
    }
    // Start alignment: first item flush with container left.
    XER_UI_EXPECT_EQ(0.0f, rects.front().x);
}

XER_UI_TEST(FlexDistributeRowSpaceBetweenSpansContainer) {
    const Rect c{10.0f, 5.0f, 300.0f, 20.0f};
    const auto rects = XER::UI::Layout::DistributeRow(
        c, {50.0f, 50.0f, 50.0f}, {}, 10.0f, FlexAlign::SpaceBetween);
    XER_UI_EXPECT_EQ(3u, rects.size());
    XER_UI_EXPECT_EQ(10.0f, rects.front().x);
    XER_UI_EXPECT_EQ(310.0f, rects.back().Right());
    for (std::size_t i = 0; i + 1 < rects.size(); ++i) {
        XER_UI_EXPECT_FALSE(RectsOverlap(rects[i], rects[i + 1]));
    }
}

XER_UI_TEST(FlexDistributeRowCenterAndEnd) {
    const Rect c{0.0f, 0.0f, 200.0f, 20.0f};
    const auto center = XER::UI::Layout::DistributeRow(
        c, {40.0f, 40.0f}, {}, 0.0f, FlexAlign::Center);
    XER_UI_EXPECT_EQ(60.0f, center.front().x);

    const auto end = XER::UI::Layout::DistributeRow(
        c, {40.0f, 40.0f}, {}, 0.0f, FlexAlign::End);
    XER_UI_EXPECT_EQ(200.0f, end.back().Right());
}

XER_UI_TEST(FlexDistributeRowNarrowShrinksWithoutOverlap) {
    const Rect c{0.0f, 0.0f, 120.0f, 30.0f};
    const auto rects = XER::UI::Layout::DistributeRow(
        c, {100.0f, 100.0f, 100.0f}, {}, 10.0f, FlexAlign::Start);
    XER_UI_EXPECT_EQ(3u, rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i) {
        XER_UI_EXPECT_TRUE(InsideContainer(rects[i], c, 1.0f));
        XER_UI_EXPECT_TRUE(rects[i].width >= 8.0f);
        if (i > 0) XER_UI_EXPECT_FALSE(RectsOverlap(rects[i - 1], rects[i]));
    }
    // Last item must not overflow the container's right edge.
    XER_UI_EXPECT_TRUE(rects.back().Right() <= c.Right() + 1.0f);
}

XER_UI_TEST(FlexDistributeRowExtremeNarrowStillNonNegative) {
    const Rect c{0.0f, 0.0f, 30.0f, 10.0f};
    const auto rects = XER::UI::Layout::DistributeRow(
        c, {50.0f, 50.0f, 50.0f, 50.0f}, {}, 4.0f, FlexAlign::Start);
    XER_UI_EXPECT_EQ(4u, rects.size());
    for (const auto& r : rects) {
        XER_UI_EXPECT_TRUE(r.width >= 0.0f);
        XER_UI_EXPECT_TRUE(r.x >= c.x - 0.01f);
    }
}

XER_UI_TEST(FlexDistributeRowPreservesInterItemGap) {
    const Rect c{0.0f, 0.0f, 500.0f, 34.0f};
    const auto rects = XER::UI::Layout::DistributeRow(
        c, {100.0f, 80.0f, 80.0f}, {1.0f, 0.0f, 0.0f}, 8.0f, FlexAlign::Start);
    XER_UI_EXPECT_EQ(3u, rects.size());
    XER_UI_EXPECT_EQ(80.0f, rects[1].width);
    XER_UI_EXPECT_EQ(80.0f, rects[2].width);
    XER_UI_EXPECT_TRUE(rects[1].x - rects[0].Right() >= 7.5f);
    XER_UI_EXPECT_TRUE(rects[2].x - rects[1].Right() >= 7.5f);
}

namespace {

class ZeroEmptyMeasureRenderer final : public XER::UI::Rendering::Renderer {
public:
    void BeginFrame(Rect) override {}
    void EndFrame() override {}
    void FillRect(Rect, XER::UI::Theme::Color, float) override {}
    void StrokeRect(Rect, XER::UI::Theme::Color, float, float) override {}
    void DrawLine(XER::UI::Layout::Point, XER::UI::Layout::Point,
                  XER::UI::Theme::Color, float) override {}
    void DrawText(std::string_view, Rect, XER::UI::Rendering::TextStyle) override {}
    XER::UI::Layout::Size MeasureText(
        std::string_view text,
        XER::UI::Rendering::TextStyle style) const override {
        if (text.empty()) return {};
        return {static_cast<float>(text.size()) * 7.0f, style.size + 4.0f};
    }
};

} // namespace

XER_UI_TEST(FlexCenteredTextRectHonorsHorizontalCenterWithPaddingAndEmptyText) {
    ZeroEmptyMeasureRenderer r;
    const XER::UI::Rendering::TextStyle style{{1, 1, 1, 1}, 12.0f, 600};
    const Rect box{10.0f, 20.0f, 100.0f, 30.0f};

    // "Grid" = 4 * 7 = 28px wide; with h_padding = 4, centered x = 10 + 4 + (92 - 28)/2 = 46.
    const Rect centered = XER::UI::Layout::CenteredTextRect(
        r, "Grid", style, box, 4.0f, true);
    XER_UI_EXPECT_TRUE(std::abs(centered.x - 46.0f) < 0.01f);
    XER_UI_EXPECT_TRUE(centered.y > box.y);

    // Empty string must still get non-zero line height (like Direct2DRenderer).
    const Rect empty_r = XER::UI::Layout::CenteredTextRect(
        r, "", style, box, 8.0f, false);
    XER_UI_EXPECT_TRUE(empty_r.height >= 12.0f);
}
