#include "ui/css/selector_checker.h"

namespace XER::UI::CSS {

bool SelectorChecker::Match(const SelectorCheckingContext& ctx) {
    if (!ctx.element || !ctx.selector ||
        ctx.selector->compounds.empty())
        return false;
    return MatchSelector(*ctx.element, *ctx.selector, 0);
}

bool SelectorChecker::MatchSelector(const CSSElement& el,
                                    const Selector& sel,
                                    std::size_t index) {
    if (index >= sel.compounds.size()) return false;
    const CompoundSelector& comp = sel.compounds[index];
    if (!comp.Matches(el)) return false;
    if (index + 1 == sel.compounds.size()) return true;
    // compounds[index+1].combinator describes how index relates to the
    // element's side — recurse up.
    return MatchAncestor(el, sel, index + 1);
}

bool SelectorChecker::MatchAncestor(const CSSElement& el,
                                    const Selector& sel,
                                    std::size_t index) {
    const auto comb = sel.compounds[index].combinator;
    const CSSElement* p = el.Parent();
    if (comb == CompoundSelector::Combinator::kChild) {
        if (!p) return false;
        return sel.compounds[index].Matches(*p) &&
               (index + 1 == sel.compounds.size() ||
                MatchAncestor(*p, sel, index + 1));
    }
    // Descendant: any ancestor may match.
    for (; p; p = p->Parent()) {
        if (sel.compounds[index].Matches(*p) &&
            (index + 1 == sel.compounds.size() ||
             MatchAncestor(*p, sel, index + 1)))
            return true;
    }
    return false;
}

}  // namespace XER::UI::CSS
