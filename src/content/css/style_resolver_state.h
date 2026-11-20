/**
 * StyleResolverState — the short-lived per-element resolve context
 * (design doc): carries the element, its parent's ComputedStyle and
 * inline declarations into StyleResolver::ResolveStyle, and collects
 * the matched RuleData plus the produced ComputedStyle.
 */
#pragma once

#include <vector>

#include "content/css/computed_style.h"
#include "content/css/css_element.h"
#include "content/css/nth_index_cache.h"
#include "content/css/style_rule.h"

namespace XER::Content::CSS {

class StyleResolverState {
public:
    StyleResolverState(const CSSElement& el,
                       const ComputedStyle* parent_style,
                       const std::vector<StyleDeclaration>* inline_decls)
        : element_(el),
          parent_style_(parent_style),
          inline_declarations_(inline_decls) {}

    const CSSElement& element() const { return element_; }
    const ComputedStyle* parent_style() const { return parent_style_; }
    const std::vector<StyleDeclaration>* inline_declarations() const {
        return inline_declarations_;
    }
    NthIndexCache& nth_cache() { return nth_cache_; }

    // Matched RuleData accumulated during MatchAllRules.
    std::vector<const RuleData*>& matched_rules() { return matched_; }
    const std::vector<const RuleData*>& matched_rules() const {
        return matched_;
    }

    // DynamicPseudo mask accumulated by the rule collector (design
    // doc: conditional-pseudo failures still set AffectedBy*).
    std::uint8_t& dynamic_pseudo_mask() { return dynamic_pseudo_; }
    std::uint8_t dynamic_pseudo_mask() const { return dynamic_pseudo_; }

private:
    const CSSElement& element_;
    const ComputedStyle* parent_style_;
    const std::vector<StyleDeclaration>* inline_declarations_;
    NthIndexCache nth_cache_;
    std::vector<const RuleData*> matched_;
    std::uint8_t dynamic_pseudo_ = 0;
};

}  // namespace XER::Content::CSS
