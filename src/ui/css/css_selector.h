/**
 * CSSSelector — parsed selector representation (design doc: complex
 * selectors are arrays of compounds; the right-most compound decides
 * which RuleSet index map holds the rule).
 */
#pragma once

#include <string>
#include <vector>

#include "ui/css/css_element.h"

namespace XER::UI::CSS {

struct SimpleSelector {
    enum class Type : std::uint8_t {
        kTag, kId, kClass, kAttribute, kUniversal,
        kFocus, kHover, kActive,
        kFirstChild, kLastChild, kNthChild,
    };
    Type type = Type::kTag;
    std::string value;       // tag / id / class / attribute name
    std::string attr_value;  // [attr=val]
    int nth_a = 0;           // :nth-child(an+b) — a
    int nth_b = 0;           // :nth-child(an+b) — b
};

struct CompoundSelector {
    // Descendant vs child combinator linking to the PREVIOUS compound.
    enum class Combinator : std::uint8_t { kSelf, kDescendant, kChild };
    Combinator combinator = Combinator::kSelf;
    std::vector<SimpleSelector> simples;

    bool Matches(const CSSElement& el) const;
};

// Parsed selector: compounds right-to-left; index 0 = right-most.
struct Selector {
    std::vector<CompoundSelector> compounds;
};

// Parses a small selector subset: "tag", "*", ".cls", "#id", "[attr]",
// "[attr=val]", ":focus"/":hover"/":active",
// ":first-child"/":last-child"/":nth-child(an+b)", descendant and
// child combinators ("a b", "a > b").
Selector ParseSelector(const std::string& text);

// Comma-separated selector list → one Selector per alternative
// (selector-list alternatives; each alternative becomes its own rule).
std::vector<Selector> ParseSelectorList(const std::string& text);

}  // namespace XER::UI::CSS
