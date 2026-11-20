/**
 * ComputedStyle — the resolved property set for an element (design doc:
 * produced by StyleResolver::ResolveStyle after matched rules cascade,
 * then combined with inline style and inherited parent values).
 * Properties are stored name->value; host code reads typed accessors.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "content/css/style_rule_fwd.h"

namespace XER::Content::CSS {

// Properties that inherit from the parent's computed style when the
// element's own style does not set them (design doc: "combined with
// inline style and parent style to get computed-style").
inline bool IsInheritedProperty(std::string_view property) {
    // Text/font/color family — the usual inherited set.
    return property == "color" || property.starts_with("font") ||
           property.starts_with("text") || property == "line-height" ||
           property == "letter-spacing" || property == "visibility" ||
           property == "direction" || property == "white-space" ||
           property == "cursor" || property == "quotes";
}

class ComputedStyle {
public:
    void Set(std::string property, std::string value) {
        values_[std::move(property)] = std::move(value);
    }
    const std::string* Get(const std::string& property) const {
        const auto it = values_.find(property);
        return it == values_.end() ? nullptr : &it->second;
    }
    bool empty() const { return values_.empty(); }
    const std::map<std::string, std::string>& values() const {
        return values_;
    }

    // Design doc: inline declarations win over every matched rule —
    // applied last in the cascade.
    void ApplyInline(const std::vector<StyleDeclaration>& decls) {
        for (const auto& d : decls) Set(d.property, d.value);
    }

    // Design doc: inherited properties fall back to the parent's
    // computed value when the cascade set nothing.
    void InheritFrom(const ComputedStyle& parent) {
        for (const auto& [prop, value] : parent.values_) {
            if (!IsInheritedProperty(prop)) continue;
            if (values_.find(prop) == values_.end())
                values_.emplace(prop, value);
        }
    }

    // Design doc: AffectedBy* flags — pseudo-state rules that matched;
    // hover/active changes only recalc elements with these set.
    void SetAffectedByHover(bool v) { affected_by_hover_ = v; }
    void SetAffectedByActive(bool v) { affected_by_active_ = v; }
    void SetAffectedByFocus(bool v) { affected_by_focus_ = v; }
    bool AffectedByHover() const { return affected_by_hover_; }
    bool AffectedByActive() const { return affected_by_active_; }
    bool AffectedByFocus() const { return affected_by_focus_; }

private:
    std::map<std::string, std::string> values_;
    bool affected_by_hover_ = false;
    bool affected_by_active_ = false;
    bool affected_by_focus_ = false;
};

}  // namespace XER::Content::CSS
