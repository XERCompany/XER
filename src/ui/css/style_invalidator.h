/**
 * StyleInvalidator — pushes pending invalidation sets down the DOM
 * (design doc: Invalidate() recurses depth-first applying each node's
 * pending sets; nodes whose self-features match a set get
 * needs_recalc; sets propagate to all descendants).
 */
#pragma once

#include <vector>

#include "ui/css/css_node.h"
#include "ui/css/invalidation_set.h"
#include "ui/css/pending_invalidations.h"

namespace XER::UI::CSS {

class StyleInvalidator {
public:
    // Applies pending sets at |root| (plus any |inherited| sets),
    // marking needs_recalc / child_needs_recalc. Returns true when at
    // least one node was invalidated.
    static bool Invalidate(CSSNode& root, PendingInvalidations& pending,
                           std::vector<InvalidationSet> inherited = {});

private:
    static void InvalidateSubtree(CSSNode& node,
                                  std::vector<InvalidationSet>& sets);
};

}  // namespace XER::UI::CSS
