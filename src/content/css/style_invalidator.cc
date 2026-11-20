#include "content/css/style_invalidator.h"

#include <iterator>

namespace XER::Content::CSS {

bool StyleInvalidator::Invalidate(CSSNode& root,
                                  PendingInvalidations& pending,
                                  std::vector<InvalidationSet> inherited) {
    auto sets = pending.Take(&root);
    sets.insert(sets.end(),
                std::make_move_iterator(inherited.begin()),
                std::make_move_iterator(inherited.end()));
    if (sets.empty()) return false;
    root.needs_invalidation = true;
    InvalidateSubtree(root, sets);
    return true;
}

void StyleInvalidator::InvalidateSubtree(
    CSSNode& node, std::vector<InvalidationSet>& active_sets) {
    bool child_marked = false;
    for (auto& child : node.children) {
        for (const auto& set : active_sets)
            if (set.whole_subtree() || set.Matches(*child))
                child->needs_recalc = true;
        InvalidateSubtree(*child, active_sets);
        if (child->needs_recalc || child->child_needs_recalc)
            child_marked = true;
    }
    node.child_needs_recalc = child_marked;
}

}  // namespace XER::Content::CSS
