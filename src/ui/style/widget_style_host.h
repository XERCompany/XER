#ifndef XER_UI_STYLE_WIDGET_STYLE_HOST_H
#define XER_UI_STYLE_WIDGET_STYLE_HOST_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "content/css/css_node.h"
#include "content/css/document_style.h"
#include "content/css/tree_scope.h"
#include "widgets/widget.h"

namespace XER::UI::CSS {

// WidgetStyleHost — bridges the widget tree into the canonical
// XER::Content::CSS style engine (design doc: the host owns a
// TreeScope + DocumentStyle; each Update mirrors widget identity,
// attributes and interaction state into a CSSNode shadow tree, feeds
// the engine's FooChangedForElement entry points, pushes pending
// invalidations, recalculates and writes ComputedStyle back onto the
// widgets).
//
// Usage:
//   WidgetStyleHost host;
//   host.SetStyleSheet("button { color: #fff } button:hover { color: #f0f }");
//   // before painting (e.g. in Window frame callback):
//   host.Update(root_widget);
//   // widgets read GetComputedStyle()/StyleValue() in Render/Layout.
class WidgetStyleHost {
public:
    WidgetStyleHost() = default;

    // Replaces all active sheets with one parsed from |css_text|.
    void SetStyleSheet(std::string_view css_text);
    // Appends a sheet (later sheets win on equal specificity).
    void AddStyleSheet(std::string_view css_text);
    // Drops all sheets; the next Update resolves empty styles.
    void ClearStyleSheets();

    // Optional sink invoked for every widget whose style was
    // recalculated during Update (theme mapping, repaints...).
    void SetApplyCallback(
        std::function<void(Widgets::Widget&,
                           const Content::CSS::ComputedStyle&)> cb);

    // Syncs the widget subtree, invalidates and recalculates styles.
    // Returns the number of widgets whose computed style changed.
    int Update(const std::shared_ptr<Widgets::Widget>& root);

    // Last computed style for |widget| (nullptr before first Update).
    std::shared_ptr<const Content::CSS::ComputedStyle> StyleFor(
        const Widgets::Widget& widget) const;

private:
    // CSSNode is final, so the widget association lives in widget_of_
    // rather than a subclass.
    using MirrorNode = Content::CSS::CSSNode;

    void SyncWidget(const std::shared_ptr<Widgets::Widget>& w,
                    MirrorNode& node);
    void MarkLocalChange(MirrorNode& node);

    Content::CSS::TreeScope scope_;
    Content::CSS::DocumentStyle doc_{scope_};
    std::unique_ptr<MirrorNode> root_mirror_;
    std::unordered_map<Widgets::Widget*, MirrorNode*> mirror_;
    std::unordered_map<const MirrorNode*, Widgets::Widget*> widget_of_;
    std::unordered_map<const Content::CSS::CSSNode*,
                       Content::CSS::ComputedStyle>
        last_style_;
    std::function<void(Widgets::Widget&,
                       const Content::CSS::ComputedStyle&)>
        apply_;
    bool sheets_dirty_ = false;
};

}  // namespace XER::UI::CSS

#endif  // XER_UI_STYLE_WIDGET_STYLE_HOST_H
