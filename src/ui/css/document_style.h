/**
 * DocumentStyle — UpdateStyleAndLayoutTree entry point (design doc):
 * compiles active sheets into the RuleSet (via TreeScope), pushes
 * pending invalidations down the tree, then recalculates style for
 * invalidated nodes in shadow-including document order.
 */
#pragma once

#include <utility>
#include <vector>

#include "ui/css/computed_style.h"
#include "ui/css/css_node.h"
#include "ui/css/style_invalidator.h"
#include "ui/css/tree_scope.h"

namespace XER::UI::CSS {

class DocumentStyle {
public:
    explicit DocumentStyle(TreeScope& scope) : scope_(scope) {}

    // Push pending invalidations, then depth-first recalc; |out| gets
    // (node, style) for every invalidated node.
    void UpdateStyleAndLayoutTree(
        std::vector<std::pair<CSSNode*, ComputedStyle>>* out);

private:
    void RecalcNode(CSSNode& node,
                    std::vector<std::pair<CSSNode*, ComputedStyle>>* out);

    TreeScope& scope_;
};

}  // namespace XER::UI::CSS
