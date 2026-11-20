#include "style/widget_style_host.h"

#include <algorithm>
#include <cctype>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include "content/css/stylesheet_parser.h"

namespace XER::UI::CSS {
namespace {

// Derives an element tag from RTTI when GetStyleTag() is empty.
// Handles MSVC names ("class XER::UI::Widgets::Button") and Itanium
// mangled names ("N3XER2UI7Widgets6ButtonE"); result is lowercased.
std::string DerivedTagFor(const Widgets::Widget& w) {
    if (const auto tag = w.GetStyleTag(); !tag.empty())
        return std::string(tag);
    static std::unordered_map<std::type_index, std::string> cache;
    const std::type_index ti{typeid(w)};
    if (const auto it = cache.find(ti); it != cache.end()) {
        return it->second;
    }
    std::string raw = typeid(w).name();
    std::string tag;
    const auto space = raw.find(' ');
    const bool msvc =
        space != std::string::npos &&
        (raw.substr(0, space) == "class" ||
         raw.substr(0, space) == "struct");
    if (msvc) {
        const auto pos = raw.rfind("::");
        tag = pos == std::string::npos ? raw.substr(space + 1)
                                       : raw.substr(pos + 2);
    } else {
        // Itanium: last <length><identifier> segment is the class name.
        std::size_t i = 0;
        while (i < raw.size()) {
            if (!std::isdigit(static_cast<unsigned char>(raw[i]))) {
                ++i;
                continue;
            }
            std::size_t n = 0;
            while (i < raw.size() &&
                   std::isdigit(static_cast<unsigned char>(raw[i]))) {
                n = n * 10 + static_cast<std::size_t>(raw[i] - '0');
                ++i;
            }
            if (n > 0 && i + n <= raw.size()) {
                tag = raw.substr(i, n);
                i += n;
            }
        }
        if (tag.empty()) tag = raw;
    }
    for (auto& c : tag) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    cache.emplace(ti, tag);
    return tag;
}

bool SameIdentity(const std::vector<std::string>& a,
                  const std::vector<std::string>& b) {
    return a == b;
}

}  // namespace

void WidgetStyleHost::SetStyleSheet(std::string_view css_text) {
    scope_.StyleSheets().Clear();
    scope_.AppendActiveStyleSheets(
        Content::CSS::ParseStyleSheet(std::string(css_text)));
    sheets_dirty_ = true;
}

void WidgetStyleHost::AddStyleSheet(std::string_view css_text) {
    scope_.AppendActiveStyleSheets(
        Content::CSS::ParseStyleSheet(std::string(css_text)));
    sheets_dirty_ = true;
}

void WidgetStyleHost::ClearStyleSheets() {
    scope_.StyleSheets().Clear();
    sheets_dirty_ = true;
}

void WidgetStyleHost::SetApplyCallback(
    std::function<void(Widgets::Widget&,
                       const Content::CSS::ComputedStyle&)> cb) {
    apply_ = std::move(cb);
}

std::shared_ptr<const Content::CSS::ComputedStyle>
WidgetStyleHost::StyleFor(const Widgets::Widget& widget) const {
    const auto it = mirror_.find(
        const_cast<Widgets::Widget*>(&widget));
    if (it == mirror_.end()) return nullptr;
    const auto st = last_style_.find(it->second);
    if (st == last_style_.end()) return nullptr;
    return std::make_shared<Content::CSS::ComputedStyle>(st->second);
}

void WidgetStyleHost::MarkLocalChange(MirrorNode& node) {
    node.SetNeedsStyleRecalc(Content::CSS::kLocalStyleChange);
    if (node.parent) node.parent->SetChildNeedsStyleRecalc();
}

void WidgetStyleHost::SyncWidget(
    const std::shared_ptr<Widgets::Widget>& w, MirrorNode& node) {
    auto& engine = scope_.Resolver();

    // Tag: static per type, never triggers changes.
    node.tag = DerivedTagFor(*w);

    // id.
    const std::string new_id{w->StyleId()};
    if (new_id != node.id) {
        const bool recalc =
            (!node.id.empty() && engine.IdChangedForElement(node.id)) ||
            (!new_id.empty() && engine.IdChangedForElement(new_id));
        node.id = new_id;
        if (recalc) MarkLocalChange(node);
    }

    // classes: added classes also push descendant invalidation sets
    // (design doc: PendingInvalidations::ScheduleInvalidationSetsForNode).
    const auto& want = w->StyleClasses();
    if (!SameIdentity(node.classes, want)) {
        for (const auto& c : want) {
            if (!node.HasClass(c)) {
                if (engine.ClassChangedForElement(c))
                    MarkLocalChange(node);
                scope_.Pending().ScheduleInvalidationSetsForNode(
                    &node, engine.InvalidationSetsForClassChange(c));
            }
        }
        for (const auto& c : node.classes) {
            const bool still = std::find(want.begin(), want.end(), c)
                               != want.end();
            if (!still && engine.ClassChangedForElement(c))
                MarkLocalChange(node);
        }
        node.classes = want;
    }

    // attributes: widget pairs + synthesized "disabled".
    std::map<std::string, std::string, std::less<>> want_attrs;
    for (const auto& [n, v] : w->StyleAttributes())
        want_attrs[n] = v;
    if (!w->IsEnabled()) want_attrs["disabled"] = "true";
    for (const auto& [n, v] : want_attrs) {
        const auto it = node.attr_values.find(n);
        if (it == node.attr_values.end() || it->second != v) {
            if (engine.AttributeChangedForElement(n))
                MarkLocalChange(node);
        }
    }
    for (const auto& [n, v] : node.attr_values) {
        if (want_attrs.find(n) == want_attrs.end() &&
            engine.AttributeChangedForElement(n))
            MarkLocalChange(node);
    }
    node.attr_values = std::move(want_attrs);
    node.attributes.clear();
    for (const auto& [n, v] : node.attr_values)
        node.attributes.push_back(n);

    // pseudo-state changes (design doc: hover/active recalc only when
    // the current computed style has the matching AffectedBy* flag).
    if (node.focused != w->IsFocused()) {
        node.focused = w->IsFocused();
        if (engine.FocusedChangedForElement()) MarkLocalChange(node);
    }
    if (node.hovered != w->IsHovered()) {
        node.hovered = w->IsHovered();
        const auto it = last_style_.find(&node);
        if (it != last_style_.end() &&
            engine.PseudoStateChangedForElement(it->second, "hover"))
            MarkLocalChange(node);
    }
    if (node.active != w->IsActive()) {
        node.active = w->IsActive();
        const auto it = last_style_.find(&node);
        if (it != last_style_.end() &&
            engine.PseudoStateChangedForElement(it->second, "active"))
            MarkLocalChange(node);
    }

    // inline declarations (style="..." equivalent).
    std::vector<Content::CSS::StyleDeclaration> decls;
    decls.reserve(w->InlineStyle().size());
    for (const auto& [p, v] : w->InlineStyle())
        decls.push_back({p, v});
    if (decls.size() != node.inline_declarations.size() ||
        !std::equal(decls.begin(), decls.end(),
                    node.inline_declarations.begin(),
                    [](const auto& a, const auto& b) {
                        return a.property == b.property &&
                               a.value == b.value;
                    })) {
        node.inline_declarations = std::move(decls);
        MarkLocalChange(node);
    }

    // children: reuse mirrors while the ordered widget sequence
    // matches; any divergence rebuilds this level (reused nodes keep
    // their identity/history, new nodes arrive with kReattach).
    const auto& kids = w->GetChildren();
    bool same = kids.size() == node.children.size();
    if (same) {
        for (std::size_t i = 0; i < kids.size(); ++i) {
            const auto it = widget_of_.find(node.children[i].get());
            if (it == widget_of_.end() ||
                it->second != kids[i].get()) {
                same = false;
                break;
            }
        }
    }
    if (same) {
        for (std::size_t i = 0; i < kids.size(); ++i)
            SyncWidget(kids[i], *node.children[i]);
        return;
    }
    // Reparenting: detach each old child keyed by its widget so a
    // reordered/re-added widget keeps its mirror (style history,
    // invalidation bits). Leftovers are removed subtrees.
    std::unordered_map<Widgets::Widget*, std::unique_ptr<MirrorNode>>
        detached;
    for (auto& old : node.children) {
        const auto it = widget_of_.find(old.get());
        if (it != widget_of_.end())
            detached.emplace(it->second, std::move(old));
    }
    node.children.clear();
    node.SetNeedsStyleRecalc(Content::CSS::kSubtreeStyleChange);
    for (const auto& kid : kids) {
        std::unique_ptr<MirrorNode> child;
        if (const auto it = detached.find(kid.get());
            it != detached.end()) {
            child = std::move(it->second);
            detached.erase(it);
            child->parent = &node;
        } else {
            child = std::make_unique<MirrorNode>();
            child->parent = &node;
            child->SetNeedsStyleRecalc(Content::CSS::kReattach);
            widget_of_[child.get()] = kid.get();
            mirror_[kid.get()] = child.get();
        }
        MirrorNode* raw = child.get();
        node.children.push_back(std::move(child));
        SyncWidget(kid, *raw);
    }
    // Whatever stayed detached is a removed subtree — drop its maps.
    for (auto& [w, dead] : detached) {
        std::vector<MirrorNode*> stack{dead.get()};
        while (!stack.empty()) {
            MirrorNode* mn = stack.back();
            stack.pop_back();
            const auto it = widget_of_.find(mn);
            if (it != widget_of_.end()) {
                // Erase mirror_ only when it still points at the dead
                // node — the widget may already own a fresh mirror
                // elsewhere (cross-parent reparent processed early).
                const auto mit = mirror_.find(it->second);
                if (mit != mirror_.end() && mit->second == mn)
                    mirror_.erase(mit);
                widget_of_.erase(it);
            }
            last_style_.erase(mn);
            for (auto& c : mn->children) stack.push_back(c.get());
        }
    }
}

int WidgetStyleHost::Update(const std::shared_ptr<Widgets::Widget>& root) {
    if (!root) return 0;
    if (!root_mirror_ || widget_of_.count(root_mirror_.get()) == 0 ||
        widget_of_[root_mirror_.get()] != root.get() ||
        sheets_dirty_) {
        mirror_.clear();
        widget_of_.clear();
        last_style_.clear();
        root_mirror_ = std::make_unique<MirrorNode>();
        widget_of_[root_mirror_.get()] = root.get();
        root_mirror_->SetNeedsStyleRecalc(Content::CSS::kReattach);
        mirror_[root.get()] = root_mirror_.get();
        scope_.SetRoot(root_mirror_.get());
        scope_.Pending().Clear();
        sheets_dirty_ = false;
    }
    SyncWidget(root, *root_mirror_);

    std::vector<std::pair<Content::CSS::CSSNode*,
                          Content::CSS::ComputedStyle>>
        out;
    doc_.UpdateStyleAndLayoutTree(&out);
    for (auto& [node, style] : out) {
        last_style_[node] = style;
        auto cs = std::make_shared<Content::CSS::ComputedStyle>(
            std::move(style));
        const auto it = widget_of_.find(node);
        if (it != widget_of_.end() && it->second) {
            it->second->SetComputedStyle(cs);
            if (apply_) apply_(*it->second, *cs);
        }
    }
    return static_cast<int>(out.size());
}

}  // namespace XER::UI::CSS
