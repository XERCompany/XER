/**
 * ElementRuleCollector — gathers matching RuleData for one element
 * (design doc: CollectMatchingRulesForList is the core of all paths —
 * builds a SelectorCheckingContext and calls checker.Match per RuleData,
 * appending hits via DidMatchRule).
 */
#pragma once

#include <vector>

#include "content/css/css_element.h"
#include "content/css/nth_index_cache.h"
#include "content/css/style_rule.h"

namespace XER::Content::CSS {

// Resolve context for one element (design doc).
struct ElementResolveContext {
    const CSSElement* element = nullptr;
    NthIndexCache* nth_cache = nullptr;
    // Out: DynamicPseudo mask of rules that failed only on dynamic
    // pseudos — feeds the ComputedStyle AffectedBy* flags (design doc).
    std::uint8_t* dynamic_pseudo_out = nullptr;
};

struct MatchRequest {
    CascadeOrigin origin = CascadeOrigin::kAuthor;
};

class ElementRuleCollector {
public:
    ElementRuleCollector(ElementResolveContext ctx, MatchRequest req)
        : ctx_(ctx), request_(req) {}

    // Tests each RuleData in |list| against the element; matches are
    // appended via DidMatchRule (design doc).
    void CollectMatchingRulesForList(
        const std::vector<const RuleData*>& list);

    // Design doc: called for each matching RuleData — records it in
    // matched_rules_ (virtual seam for subclasses that sort/split).
    virtual void DidMatchRule(const RuleData* data) {
        matched_.push_back(data);
    }

    const std::vector<const RuleData*>& matched_rules() const {
        return matched_;
    }
    MatchRequest request() const { return request_; }

private:
    ElementResolveContext ctx_;
    MatchRequest request_;
    std::vector<const RuleData*> matched_;
};

}  // namespace XER::Content::CSS
