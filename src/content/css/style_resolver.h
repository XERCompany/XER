/**
 * StyleResolver — resolves matched rules into ComputedStyle (design
 * doc: StyleForLayoutObject → ResolveStyle; MatchAllRules walks
 * User Agent → User → Author origins, MatchAuthorRules splits into
 * Host/Slotted/ElementScope/PseudoPart passes; then inline style and
 * inherited parent values are combined).
 */
#pragma once

#include <vector>

#include "content/css/computed_style.h"
#include "content/css/rule_set.h"

namespace XER::Content::CSS {

class StyleResolverState;

class StyleResolver {
public:
    explicit StyleResolver(const RuleSet& rules) : rules_(rules) {}

    // Resolves the computed style for |el| (no parent/inline context).
    ComputedStyle ResolveStyle(const CSSElement& el) const;

    // Full resolve: MatchAllRules → cascade → inline declarations →
    // inherited properties from state.parent_style().
    ComputedStyle ResolveStyle(StyleResolverState& state) const;

    // Design doc: MatchAllRules — the per-origin entry points.
    void MatchAllRules(StyleResolverState& state) const;
    void MatchUserAgentRules(StyleResolverState& state) const;
    void MatchUserRules(StyleResolverState& state) const;
    void MatchAuthorRules(StyleResolverState& state) const;

    // Design doc: the passes MatchAuthorRules splits into. No shadow
    // DOM / scopes / pseudo-element parts exist in this engine yet, so
    // these are currently no-ops kept as the documented seams.
    void MatchHostRules(StyleResolverState& state) const;
    void MatchSlottedRules(StyleResolverState& state) const;
    void MatchElementScopeRules(StyleResolverState& state) const;
    void MatchPseudoPartRules(StyleResolverState& state) const;

private:
    const RuleSet& rules_;
};

}  // namespace XER::Content::CSS
