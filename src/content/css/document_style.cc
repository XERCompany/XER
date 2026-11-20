#include "content/css/document_style.h"

#include "content/css/style_resolver_state.h"

namespace XER::Content::CSS {

void DocumentStyle::UpdateStyleAndLayoutTree(
    std::vector<std::pair<CSSNode*, ComputedStyle>>* out) {
    if (!scope_.root()) return;
    UpdateActiveStyle();
    UpdateStyleInvalidationIfNeeded();
    UpdateStyle(out);
}

void DocumentStyle::UpdateActiveStyle() {
    // Sheets are appended lazily: TreeScope marks the collection dirty
    // when a sheet is added; the resolver rebuilds only then.
    scope_.RebuildActiveStyleSheetsIfNeeded();
}

void DocumentStyle::UpdateStyleInvalidationIfNeeded() {
    if (!scope_.root() || scope_.Pending().empty()) return;
    // Push phase: apply each node's pending invalidation sets
    // (depth-first over the whole tree).
    std::vector<CSSNode*> stack{scope_.root()};
    while (!stack.empty()) {
        CSSNode* n = stack.back();
        stack.pop_back();
        StyleInvalidator::Invalidate(*n, scope_.Pending());
        for (auto& c : n->children) stack.push_back(c.get());
    }
}

void DocumentStyle::UpdateStyle(
    std::vector<std::pair<CSSNode*, ComputedStyle>>* out) {
    if (!scope_.root()) return;
    // Design doc: kReattach roots take the reattach path.
    if (scope_.root()->style_change & kReattach) {
        scope_.root()->style_change = 0;
        RecalcDescendantStylesForReattach(*scope_.root(), nullptr, out);
        return;
    }
    RecalcDescendantStyles(*scope_.root(), nullptr, out);
}

ComputedStyle DocumentStyle::ResolveWithParent(
    CSSNode& node, const ComputedStyle* parent) {
    // Design doc: inline declarations + parent style combine into the
    // final computed style (StyleResolverState carries both).
    StyleResolverState state(node, parent, &node.inline_declarations);
    return scope_.Resolver().StyleForElement(state);
}

void DocumentStyle::RecalcDescendantStyles(
    CSSNode& node, const ComputedStyle* parent_style,
    std::vector<std::pair<CSSNode*, ComputedStyle>>* out) {
    const ComputedStyle* own = parent_style;
    ComputedStyle resolved;
    if (node.needs_recalc) {
        node.needs_recalc = false;
        node.needs_invalidation = false;
        const bool subtree = (node.style_change & kSubtreeStyleChange) != 0;
        if (node.style_change & kReattach) {
            node.style_change = 0;
            RecalcDescendantStylesForReattach(node, parent_style, out);
            return;
        }
        node.style_change = 0;
        resolved = ResolveWithParent(node, parent_style);
        if (out) out->emplace_back(&node, resolved);
        own = &resolved;
        if (subtree) {
            // kSubtreeStyleChange: children must recalc too — their
            // inherited inputs changed.
            for (auto& c : node.children) c->needs_recalc = true;
        }
    }
    node.child_needs_recalc = false;
    for (auto& c : node.children)
        RecalcDescendantStyles(*c, own, out);
}

void DocumentStyle::RecalcDescendantStylesForReattach(
    CSSNode& node, const ComputedStyle* parent_style,
    std::vector<std::pair<CSSNode*, ComputedStyle>>* out) {
    // Every node in the subtree resolves fresh — no invalidation flags
    // consulted (design doc: RecalcFooStyleForReattach).
    node.needs_recalc = false;
    node.needs_invalidation = false;
    node.child_needs_recalc = false;
    node.child_needs_invalidation = false;
    node.style_change = 0;
    ComputedStyle resolved = ResolveWithParent(node, parent_style);
    if (out) out->emplace_back(&node, resolved);
    for (auto& c : node.children)
        RecalcDescendantStylesForReattach(*c, &resolved, out);
}

}  // namespace XER::Content::CSS
