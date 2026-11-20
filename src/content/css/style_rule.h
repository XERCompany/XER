/**
 * StyleRule / StyleSheet / RuleData (design doc): a rule is a selector
 * plus declarations; RuleData carries the rule's position/specificity
 * for cascade ordering inside RuleSet maps.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "content/css/css_selector.h"
#include "content/css/style_rule_fwd.h"

namespace XER::Content::CSS {

struct StyleRule {
    Selector selector;
    std::vector<StyleDeclaration> declarations;
    std::string text;  // original selector text (diagnostics)
};

enum class CascadeOrigin : std::uint8_t { kUserAgent, kUser, kAuthor };

struct StyleSheet {
    CascadeOrigin origin = CascadeOrigin::kAuthor;
    std::vector<StyleRule> rules;
};

// RuleData: rule + where it sits in the cascade.
struct RuleData {
    const StyleRule* rule = nullptr;
    CascadeOrigin origin = CascadeOrigin::kAuthor;
    int position = 0;        // source order
    int specificity = 0;     // (id,class,tag) packed: id<<16|cls<<8|tag
};

// Packs (ids, classes, tags) into the doc's specificity tuple.
inline int Specificity(int ids, int classes, int tags) {
    return (ids << 16) | (classes << 8) | tags;
}

}  // namespace XER::Content::CSS
