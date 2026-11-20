/**
 * PendingInvalidations — PendingInvalidationsMap (design doc: DOM
 * changes accumulate invalidation sets per node until style is read;
 * then ScheduleInvalidationSetsForNode pushes them down the subtree).
 */
#pragma once

#include <iterator>
#include <map>
#include <vector>

#include "content/css/invalidation_set.h"

namespace XER::Content::CSS {

class PendingInvalidations {
public:
    // Records sets to be applied to |node|'s subtree at invalidation
    // time (design doc).
    void ScheduleInvalidationSetsForNode(
        const CSSElement* node, std::vector<InvalidationSet> sets) {
        auto& dst = map_[node];
        dst.insert(dst.end(),
                   std::make_move_iterator(sets.begin()),
                   std::make_move_iterator(sets.end()));
    }

    // Moves out all pending sets for |node| (empty when none).
    std::vector<InvalidationSet> Take(const CSSElement* node) {
        std::vector<InvalidationSet> out;
        const auto it = map_.find(node);
        if (it != map_.end()) {
            out = std::move(it->second);
            map_.erase(it);
        }
        return out;
    }
    bool empty() const { return map_.empty(); }
    void Clear() { map_.clear(); }

private:
    std::map<const CSSElement*, std::vector<InvalidationSet>> map_;
};

// Design doc name: PendingInvalidationsMap — the map from DOM Node to
// invalidation sets to apply on updates to that node.
using PendingInvalidationsMap = PendingInvalidations;

}  // namespace XER::Content::CSS
