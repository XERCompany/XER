/**
 * ComputedStyle — the resolved property set for an element (design doc:
 * produced by StyleResolver::ResolveStyle after matched rules cascade).
 * Properties are stored name->value; host code reads typed accessors.
 */
#pragma once

#include <map>
#include <string>

namespace XER::UI::CSS {

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

}  // namespace XER::UI::CSS
