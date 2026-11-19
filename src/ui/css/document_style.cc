#include "ui/css/document_style.h"

namespace XER::UI::CSS {

void DocumentStyle::UpdateStyleAndLayoutTree(
    std::vector<std::pair<CSSNode*, ComputedStyle>>* out) {
    CSSNode* root = scope_.root();
    if (!root) return;
    // Push phase: apply each node's pending invalidation sets
    // (depth-first over the whole tree).
    std::vector<CSSNode*> stack{root};
    while (!stack.empty()) {
        CSSNode* n = stack.back();
        stack.pop_back();
        StyleInvalidator::Invalidate(*n, scope_.Pending());
        for (auto& c : n->children) stack.push_back(c.get());
    }
    // Recalc phase in document order.
    RecalcNode(*root, out);
}

void DocumentStyle::RecalcNode(
    CSSNode& node,
    std::vector<std::pair<CSSNode*, ComputedStyle>>* out) {
    if (node.needs_recalc) {
        node.needs_recalc = false;
        node.needs_invalidation = false;
        if (out)
            out->emplace_back(
                &node, scope_.Resolver().StyleForElement(node));
    }
    node.child_needs_recalc = false;
    for (auto& c : node.children) RecalcNode(*c, out);
}

}  // namespace XER::UI::CSS
