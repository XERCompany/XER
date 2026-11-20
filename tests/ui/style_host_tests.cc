// WidgetStyleHost + UiRenderHost coverage: the XER::Content::CSS
// bridge mirrors a live widget tree, applies selectors/inline style /
// pseudo-state invalidation, and the render worker pool pre-spawns and
// checks out xer_engine_ui_render.exe processes (Windows only).

#include <memory>
#include <string>
#include <string_view>

#include "core/render_host.h"
#include "rendering/renderer.h"
#include "rendering/render_stream.h"
#include "style/style_values.h"
#include "style/widget_style_host.h"
#include "test_harness.h"
#include "widgets/widget.h"
#include "widgets/widget_tree.h"

namespace {

using XER::UI::CSS::WidgetStyleHost;
using XER::UI::Widgets::Widget;
using XER::UI::Widgets::WidgetTree;

class CssBox final : public Widget {
public:
    explicit CssBox(std::string tag) : tag_(std::move(tag)) {}
    std::string_view GetStyleTag() const override { return tag_; }
    void Render(XER::UI::Rendering::Renderer&,
                const XER::UI::Theme::Tokens&) override {}
    void Layout(const XER::UI::Layout::Rect&) override {}
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    XER::UI::Layout::Size GetMinSize() const override { return {}; }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {};
    }
    XER::UI::Layout::Size GetMaxSize() const override { return {}; }

private:
    std::string tag_;
};

std::string ValueOf(const Widget& w, const char* prop) {
    const auto* v = w.StyleValue(prop);
    return v ? *v : std::string{};
}

XER_UI_TEST(StyleHost_AppliesTagClassDescendant) {
    auto root = std::make_shared<CssBox>("panel");
    auto btn = std::make_shared<CssBox>("button");
    btn->AddStyleClass("primary");
    root->AddChild(btn);

    WidgetStyleHost host;
    host.SetStyleSheet(
        "button { color: #112233 } "
        ".primary { font-size: 20px } "
        "panel .primary { border-color: rgb(255, 0, 0) }");
    const int n = host.Update(root);

    XER::UI::Test::ExpectEqual(2, n, "update count", __FILE__, __LINE__);
    XER::UI::Test::ExpectEqual(std::string("#112233"),
                               ValueOf(*btn, "color"), "tag rule",
                               __FILE__, __LINE__);
    XER::UI::Test::ExpectEqual(std::string("20px"),
                               ValueOf(*btn, "font-size"),
                               "class rule", __FILE__, __LINE__);
    XER::UI::Test::ExpectEqual(std::string("rgb(255, 0, 0)"),
                               ValueOf(*btn, "border-color"),
                               "descendant rule", __FILE__, __LINE__);
}

XER_UI_TEST(StyleHost_HoverRecalcsOnlyFlagged) {
    auto root = std::make_shared<CssBox>("panel");
    auto btn = std::make_shared<CssBox>("button");
    auto other = std::make_shared<CssBox>("label");
    root->AddChild(btn);
    root->AddChild(other);

    WidgetStyleHost host;
    host.SetStyleSheet(
        "button { color: #111111 } "
        "button:hover { color: #ffffff } "
        "label { color: #222222 }");
    host.Update(root);
    XER::UI::Test::ExpectEqual(std::string("#111111"),
                               ValueOf(*btn, "color"), "before hover",
                               __FILE__, __LINE__);

    btn->SetHovered(true);
    const int n = host.Update(root);
    // Conditional-pseudo evaluation flagged the button at first
    // resolve — only it is recalculated now.
    XER::UI::Test::ExpectEqual(1, n, "hover recalc count", __FILE__,
                             __LINE__);
    XER::UI::Test::ExpectEqual(std::string("#ffffff"),
                               ValueOf(*btn, "color"), "hover color",
                               __FILE__, __LINE__);
    XER::UI::Test::ExpectEqual(std::string("#222222"),
                               ValueOf(*other, "color"),
                               "sibling untouched", __FILE__, __LINE__);

    // No mutations → no work.
    XER::UI::Test::ExpectEqual(0, host.Update(root), "no-op update",
                             __FILE__, __LINE__);
}

XER_UI_TEST(StyleHost_InlineAndAttributeSelectors) {
    auto root = std::make_shared<CssBox>("panel");
    auto inp = std::make_shared<CssBox>("input");
    inp->SetStyleAttribute("type", "text");
    inp->SetInlineStyle("color", "green");
    root->AddChild(inp);

    WidgetStyleHost host;
    host.SetStyleSheet(
        "input { color: red } input[type=text] { border: 1px }");
    host.Update(root);

    // Inline beats the matched rule; the attribute selector adds a
    // second property.
    XER::UI::Test::ExpectEqual(std::string("green"),
                               ValueOf(*inp, "color"), "inline wins",
                               __FILE__, __LINE__);
    XER::UI::Test::ExpectEqual(std::string("1px"),
                               ValueOf(*inp, "border"), "attr match",
                               __FILE__, __LINE__);
}

XER_UI_TEST(StyleHost_ReparentKeepsIdentity) {
    auto root = std::make_shared<CssBox>("panel");
    auto a = std::make_shared<CssBox>("section");
    auto b = std::make_shared<CssBox>("aside");
    auto btn = std::make_shared<CssBox>("button");
    btn->AddStyleClass("hot");
    root->AddChild(a);
    root->AddChild(b);
    a->AddChild(btn);

    WidgetStyleHost host;
    host.SetStyleSheet("section .hot { color: red } "
                       "aside .hot { color: blue }");
    host.Update(root);
    XER::UI::Test::ExpectEqual(std::string("red"),
                               ValueOf(*btn, "color"), "under section",
                               __FILE__, __LINE__);

    // Reparent btn under `aside` — ancestor scope changes the match.
    a->RemoveChild(btn);
    b->AddChild(btn);
    host.Update(root);
    XER::UI::Test::ExpectEqual(std::string("blue"),
                               ValueOf(*btn, "color"), "under aside",
                               __FILE__, __LINE__);
}

XER_UI_TEST(StyleValues_ParseColorAndLength) {
    using XER::UI::CSS::ParseColor;
    using XER::UI::CSS::ParseLength;
    const auto c1 = ParseColor("#ff8000");
    XER::UI::Test::Expect(c1 && c1->r == 1.0f && c1->g > 0.4f &&
                              c1->g < 0.6f,
                          "#rrggbb", __FILE__, __LINE__);
    const auto c2 = ParseColor("rgb(0, 128, 255)");
    XER::UI::Test::Expect(c2 && c2->b == 1.0f, "rgb()", __FILE__,
                          __LINE__);
    const auto c3 = ParseColor("white");
    XER::UI::Test::Expect(c3 && c3->r == 1.0f, "named", __FILE__,
                          __LINE__);
    XER::UI::Test::Expect(!ParseColor("bogus"), "reject", __FILE__,
                          __LINE__);
    const auto l1 = ParseLength("12px");
    XER::UI::Test::Expect(l1 && *l1 == 12.0f, "px", __FILE__, __LINE__);
    const auto l2 = ParseLength("12pt");
    XER::UI::Test::Expect(l2 && *l2 == 16.0f, "pt→px", __FILE__,
                          __LINE__);
}

XER_UI_TEST(WidgetTree_AutoStyleOnLayoutRender) {
    // WidgetTree drives Update() itself once a sheet is attached.
    auto root = std::make_shared<CssBox>("button");
    WidgetTree tree;
    tree.SetRoot(root);
    tree.SetStyleSheet("button { color: #abcdef }");
    tree.Layout({0, 0, 100, 40}, XER::UI::Theme::Tokens{});
    XER::UI::Test::ExpectEqual(std::string("#abcdef"),
                               ValueOf(*root, "color"),
                               "styled on layout", __FILE__, __LINE__);
}

XER_UI_TEST(RenderHost_WarmPoolCheckout) {
#ifdef _WIN32
    auto& host = XER::UI::Core::UiRenderHost::Instance();
    std::string err;
    const bool ok = host.EnsureWorkers(2, &err);
    // Pool semantics hold whenever spawning is possible; on machines
    // without the worker binary the call still fails gracefully.
    if (ok) {
        XER::UI::Test::Expect(host.WarmWorkerCount() >= 1,
                              "warm workers", __FILE__, __LINE__);
        auto r = host.CreateRemoteRenderer(0, &err);
        XER::UI::Test::Expect(r != nullptr, "checkout", __FILE__,
                              __LINE__);
        host.ShutdownAll();
        XER::UI::Test::ExpectEqual(0, host.WorkerCount(),
                                 "shutdown clean", __FILE__, __LINE__);
    }
#endif
}

XER_UI_TEST(RenderHost_CheckoutReleaseReturnsToPool) {
#ifdef _WIN32
    auto& host = XER::UI::Core::UiRenderHost::Instance();
    std::string err;
    if (!host.EnsureWorkers(1, &err)) {
        return;  // no worker binary — same graceful skip as above
    }
    const int warm_before = host.WarmWorkerCount();
    {
        auto renderer = host.CreateRemoteRenderer(0, &err);
        XER::UI::Test::Expect(renderer != nullptr, "checkout",
                              __FILE__, __LINE__);
        XER::UI::Test::Expect(host.WarmWorkerCount() < warm_before,
                              "worker checked out", __FILE__, __LINE__);
    }
    // ~StreamRenderer fires the release hook: the channel entry clears
    // in_use and the live worker goes back to the warm pool.
    XER::UI::Test::Expect(host.WarmWorkerCount() >= warm_before,
                          "worker released to pool", __FILE__,
                          __LINE__);
    host.ShutdownAll();
#endif
}

}  // namespace
