/**
 * DocumentStyle — the document-side UpdateStyleAndLayoutTree entry
 * point (design doc): UpdateActiveStyle compiles sheets into the
 * RuleSet, UpdateStyleInvalidationIfNeeded pushes pending invalidations
 * depth-first, UpdateStyle recalculates in document order —
 * RecalcDescendantStyles for the normal path and
 * RecalcDescendantStylesForReattach for kReattach subtrees.
 */
#pragma once

#include <utility>
#include <vector>

#include "content/css/computed_style.h"
#include "content/css/css_node.h"
#include "content/css/style_engine.h"
#include "content/css/style_invalidator.h"
#include "content/css/tree_scope.h"

namespace XER::Content::CSS {

class DocumentStyle {
public:
    explicit DocumentStyle(TreeScope& scope) : scope_(scope) {}

    // Design doc: Document::UpdateStyleAndLayoutTree —
    //   UpdateActiveStyle → UpdateStyleInvalidationIfNeeded → UpdateStyle.
    // |out| gets (node, style) for every recalculated node.
    void UpdateStyleAndLayoutTree(
        std::vector<std::pair<CSSNode*, ComputedStyle>>* out);

    // Compiles the scope's active sheets into the resolver's RuleSet
    // (design doc: UpdateActiveStyle → AppendActiveStyleSheets +
    // FindBestRuleSetAndAdd partitioning).
    void UpdateActiveStyle();

    // Pushes every pending invalidation set depth-first over the tree
    // (design doc: StyleInvalidator::Invalidate).
    void UpdateStyleInvalidationIfNeeded();

    // Traverses the element tree in document order and recalculates
    // styles for invalidated nodes (design doc: UpdateStyle →
    // RecalcDescendantStyles).
    void UpdateStyle(std::vector<std::pair<CSSNode*, ComputedStyle>>* out);

    // Design doc: the two recursive paths — normal incremental recalc
    // and the whole-subtree reattach.
    void RecalcDescendantStyles(
        CSSNode& node, const ComputedStyle* parent_style,
        std::vector<std::pair<CSSNode*, ComputedStyle>>* out);
    void RecalcDescendantStylesForReattach(
        CSSNode& node, const ComputedStyle* parent_style,
        std::vector<std::pair<CSSNode*, ComputedStyle>>* out);

private:
    ComputedStyle ResolveWithParent(CSSNode& node,
                                    const ComputedStyle* parent);

    TreeScope& scope_;
};

}  // namespace XER::Content::CSS
