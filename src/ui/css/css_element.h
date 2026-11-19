/**
 * CSSElement — minimal DOM surface the style engine needs (design doc:
 * SelectorChecker reads tag/id/classes/attributes and ancestors; the
 * host's real nodes adapt to this interface).
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace XER::UI::CSS {

class CSSElement {
public:
    virtual ~CSSElement() = default;
    virtual std::string_view TagName() const = 0;
    virtual std::string_view Id() const = 0;
    virtual bool HasClass(std::string_view name) const = 0;
    virtual bool HasAttribute(std::string_view name) const = 0;
    // Attribute value lookup for [attr=v]; default = HasAttribute.
    virtual std::string_view AttributeValue(
        std::string_view /*name*/) const { return {}; }
    virtual const CSSElement* Parent() const = 0;

    // User-action state (design doc: changes recalc only elements with
    // AffectedBy* flags on their ComputedStyle).
    virtual bool IsFocused() const { return false; }
    virtual bool IsHovered() const { return false; }
    virtual bool IsActive() const { return false; }

    // Structural indices for :nth-child etc; 0-based; -1 = unknown.
    virtual int NthChildIndex() const { return -1; }
    virtual int SiblingCount() const { return -1; }
};

}  // namespace XER::UI::CSS
