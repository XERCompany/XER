/**
 * NthIndexCache — memoizes sibling indices during one match pass
 * (design doc, "Omissions: Caching, fast reject, NthIndexCache"):
 * :nth-child evaluation asks elements for NthChildIndex, which for a
 * DOM adapter may walk siblings — cache it per parent so a rule list
 * doesn't rescan the sibling chain for every candidate.
 */
#pragma once

#include <unordered_map>

#include "content/css/css_element.h"

namespace XER::Content::CSS {

class NthIndexCache {
public:
    // 0-based index of |el| among its siblings; -1 when unknown.
    int NthIndex(const CSSElement& el) const {
        const auto it = cache_.find(&el);
        if (it != cache_.end()) return it->second;
        const int idx = el.NthChildIndex();
        cache_.emplace(&el, idx);
        return idx;
    }
    // Sibling count for :nth-*-of-type / last-child checks.
    int SiblingCount(const CSSElement& el) const {
        const auto it = counts_.find(&el);
        if (it != counts_.end()) return it->second;
        const int n = el.SiblingCount();
        counts_.emplace(&el, n);
        return n;
    }
    void clear() const {
        cache_.clear();
        counts_.clear();
    }

private:
    // mutable: the cache is logically const during a resolve pass.
    mutable std::unordered_map<const CSSElement*, int> cache_;
    mutable std::unordered_map<const CSSElement*, int> counts_;
};

}  // namespace XER::Content::CSS
