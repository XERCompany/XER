/**
 * SelectorChecker — recursive MatchSelector core (design doc): walks
 * compounds right-to-left, climbing ancestors for descendant/child
 * combinators. SelectorCheckingContext carries the element + result.
 */
#pragma once

#include "content/css/css_element.h"
#include "content/css/css_selector.h"
#include "content/css/nth_index_cache.h"

namespace XER::Content::CSS {

struct SelectorCheckingContext {
    const CSSElement* element = nullptr;
    const Selector* selector = nullptr;
    // Memoized sibling indices for :nth-* checks during this match.
    NthIndexCache* nth_cache = nullptr;
};

class SelectorChecker {
public:
    // Public entrypoint: does |selector| apply to |element|?
    static bool Match(const SelectorCheckingContext& ctx);
    // Recursive core: match compound |index| and everything left of it.
    static bool MatchSelector(const CSSElement& el,
                              const Selector& sel, std::size_t index,
                              NthIndexCache* nth_cache = nullptr);
    // Checks ONE compound's simple selectors against |el| (design doc:
    // CheckOne — tag/id/class/attr/pseudo tests inside one compound).
    static bool CheckOne(const CSSElement& el,
                         const CompoundSelector& compound,
                         NthIndexCache* nth_cache = nullptr);

    // Like Match, but dynamic pseudos (:hover/:active/:focus) always
    // pass; |ignored| accumulates a DynamicPseudo mask of what was
    // skipped (design doc: conditional-pseudo evaluation feeds the
    // ComputedStyle AffectedBy* flags).
    static bool MatchIgnoringDynamicPseudos(
        const SelectorCheckingContext& ctx, std::uint8_t* ignored);

private:
    // Finds an ancestor matching compound |index| per its combinator.
    static bool MatchAncestor(const CSSElement& el, const Selector& sel,
                              std::size_t index,
                              NthIndexCache* nth_cache,
                              std::uint8_t* ignored = nullptr);
    static bool MatchSelectorImpl(const CSSElement& el,
                                  const Selector& sel,
                                  std::size_t index,
                                  NthIndexCache* nth_cache,
                                  std::uint8_t* ignored);
};

}  // namespace XER::Content::CSS
