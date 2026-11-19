/**
 * StyleResolver — resolves matched rules into ComputedStyle (design
 * doc: cascade = origin (UA < User < Author), then specificity, then
 * source order — later wins).
 */
#pragma once

#include "ui/css/computed_style.h"
#include "ui/css/rule_set.h"

namespace XER::UI::CSS {

class StyleResolver {
public:
    explicit StyleResolver(const RuleSet& rules) : rules_(rules) {}

    // Resolves the computed style for |el|.
    ComputedStyle ResolveStyle(const CSSElement& el) const;

private:
    const RuleSet& rules_;
};

}  // namespace XER::UI::CSS
