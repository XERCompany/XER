/**
 * CSSNode — concrete CSSElement implementation for the engine's own
 * invalidation/recalc traversal (design doc: DOM nodes carry
 * NeedsStyleInvalidation / ChildNeedsStyleInvalidation /
 * NeedsStyleRecalc / ChildNeedsStyleRecalc bits).
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ui/css/css_element.h"

namespace XER::UI::CSS {

class CSSNode final : public CSSElement {
public:
    std::string tag, id;
    std::vector<std::string> classes;
    std::vector<std::string> attributes;
    std::map<std::string, std::string, std::less<>> attr_values;
    CSSNode* parent = nullptr;
    std::vector<std::unique_ptr<CSSNode>> children;
    bool focused = false;
    bool hovered = false;
    bool active = false;

    // Style state bits (design doc).
    bool needs_invalidation = false;
    bool child_needs_invalidation = false;
    bool needs_recalc = false;
    bool child_needs_recalc = false;

    std::string_view TagName() const override { return tag; }
    std::string_view Id() const override { return id; }
    bool HasClass(std::string_view n) const override {
        for (const auto& c : classes) if (c == n) return true;
        return false;
    }
    bool HasAttribute(std::string_view n) const override {
        for (const auto& a : attributes) if (a == n) return true;
        return attr_values.find(n) != attr_values.end();
    }
    std::string_view AttributeValue(std::string_view n) const override {
        const auto it = attr_values.find(n);
        return it == attr_values.end() ? std::string_view{}
                                      : it->second;
    }
    bool IsFocused() const override { return focused; }
    bool IsHovered() const override { return hovered; }
    bool IsActive() const override { return active; }
    int NthChildIndex() const override {
        if (!parent) return -1;
        for (int i = 0; i < static_cast<int>(parent->children.size()); ++i)
            if (parent->children[i].get() == this) return i;
        return -1;
    }
    int SiblingCount() const override {
        return parent ? static_cast<int>(parent->children.size()) : -1;
    }
    const CSSElement* Parent() const override { return parent; }

    CSSNode* AddChild(std::unique_ptr<CSSNode> child) {
        child->parent = this;
        children.push_back(std::move(child));
        return children.back().get();
    }
};

}  // namespace XER::UI::CSS
