/**
 * CSSSelector — parsed selector representation (design doc: complex
 * selectors are arrays of compounds; the right-most compound decides
 * which RuleSet index map holds the rule).
 */
#pragma once

#include <string>
#include <vector>

#include "content/css/css_element.h"

namespace XER::Content::CSS {

class NthIndexCache;  // fwd

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

// Dynamic pseudo mask bits (design doc: elements whose style fails
// only on a dynamic pseudo carry AffectedBy* on their ComputedStyle).
enum DynamicPseudo : std::uint8_t {
    kNoDynamic = 0,
    kHoverBit = 1 << 0,
    kActiveBit = 1 << 1,
    kFocusBit = 1 << 2,
};

struct CompoundSelector {
    // Descendant vs child combinator linking to the PREVIOUS compound.
    enum class Combinator : std::uint8_t { kSelf, kDescendant, kChild };
    Combinator combinator = Combinator::kSelf;
    std::vector<SimpleSelector> simples;

    // |ignored|: when non-null, dynamic pseudos always "match" and the
    // mask accumulates which kinds were skipped (conditional-pseudo
    // matching for AffectedBy* flags).
    bool Matches(const CSSElement& el,
                 const NthIndexCache* cache = nullptr,
                 std::uint8_t* ignored = nullptr) const;
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

}  // namespace XER::Content::CSS
