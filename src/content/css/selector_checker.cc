#include "content/css/selector_checker.h"

namespace XER::Content::CSS {

bool SelectorChecker::Match(const SelectorCheckingContext& ctx) {
    if (!ctx.element || !ctx.selector ||
        ctx.selector->compounds.empty())
        return false;
    return MatchSelector(*ctx.element, *ctx.selector, 0,
                         ctx.nth_cache);
}

bool SelectorChecker::CheckOne(const CSSElement& el,
                               const CompoundSelector& compound,
                               NthIndexCache* nth_cache) {
    return compound.Matches(el, nth_cache);
}

bool SelectorChecker::MatchSelector(const CSSElement& el,
                                    const Selector& sel,
                                    std::size_t index,
                                    NthIndexCache* nth_cache) {
    return MatchSelectorImpl(el, sel, index, nth_cache, nullptr);
}

bool SelectorChecker::MatchSelectorImpl(
    const CSSElement& el, const Selector& sel, std::size_t index,
    NthIndexCache* nth_cache, std::uint8_t* ignored) {
    if (index >= sel.compounds.size()) return false;
    const CompoundSelector& comp = sel.compounds[index];
    if (!comp.Matches(el, nth_cache, ignored)) return false;
    if (index + 1 == sel.compounds.size()) return true;
    // compounds[index+1].combinator describes how index relates to the
    // element's side — recurse up.
    return MatchAncestor(el, sel, index + 1, nth_cache, ignored);
}

bool SelectorChecker::MatchIgnoringDynamicPseudos(
    const SelectorCheckingContext& ctx, std::uint8_t* ignored) {
    if (!ctx.element || !ctx.selector ||
        ctx.selector->compounds.empty())
        return false;
    return MatchSelectorImpl(*ctx.element, *ctx.selector, 0,
                             ctx.nth_cache, ignored);
}

bool SelectorChecker::MatchAncestor(const CSSElement& el,
                                    const Selector& sel,
                                    std::size_t index,
                                    NthIndexCache* nth_cache,
                                    std::uint8_t* ignored) {
    const auto comb = sel.compounds[index].combinator;
    const CSSElement* p = el.Parent();
    if (comb == CompoundSelector::Combinator::kChild) {
        if (!p) return false;
        return sel.compounds[index].Matches(*p, nth_cache, ignored) &&
               (index + 1 == sel.compounds.size() ||
                MatchAncestor(*p, sel, index + 1, nth_cache, ignored));
    }
    // Descendant: any ancestor may match.
    for (; p; p = p->Parent()) {
        if (sel.compounds[index].Matches(*p, nth_cache, ignored) &&
            (index + 1 == sel.compounds.size() ||
             MatchAncestor(*p, sel, index + 1, nth_cache, ignored)))
            return true;
    }
    return false;
}

}  // namespace XER::Content::CSS
