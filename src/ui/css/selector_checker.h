/**
 * SelectorChecker — recursive MatchSelector core (design doc): walks
 * compounds right-to-left, climbing ancestors for descendant/child
 * combinators. SelectorCheckingContext carries the element + result.
 */
#pragma once

#include "ui/css/css_element.h"
#include "ui/css/css_selector.h"

namespace XER::UI::CSS {

struct SelectorCheckingContext {
    const CSSElement* element = nullptr;
    const Selector* selector = nullptr;
};

class SelectorChecker {
public:
    // Public entrypoint: does |selector| apply to |element|?
    static bool Match(const SelectorCheckingContext& ctx);
    // Recursive core: match compound |index| and everything left of it.
    static bool MatchSelector(const CSSElement& el,
                              const Selector& sel, std::size_t index);

private:
    // Finds an ancestor matching compound |index| per its combinator.
    static bool MatchAncestor(const CSSElement& el, const Selector& sel,
                              std::size_t index);
};

}  // namespace XER::UI::CSS
