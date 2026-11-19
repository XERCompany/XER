#include "debug_map_ui.h"
#include "debug_internal.h"

#include "../widgets/widget.h"
#include "../layout/geometry.h"

#ifdef XER_UI_HAS_LIBXML2
#include <libxml/tree.h>
#endif

#include <cctype>
#include <format>
#include <string> 
#include <string_view>
#include <typeinfo>
#include <unordered_set>
#include <vector>

namespace XER::UI::Debug {
namespace {

namespace Widgets = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;

#ifndef XER_UI_HAS_LIBXML2
// libxml2-less build: the inspector still answers with a valid (empty)
// document instead of failing to link.
std::string UnavailableXml() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<DebugMapUI unavailable=\"libxml2\" nodes=\"0\"/>\n";
}
#endif

std::string Fmt(float v) { return std::format("{:.4g}", v); }

// ---------------------------------------------------------------------------
// Type names — implementations live in Debug::internal (debug_internal.h)
// ---------------------------------------------------------------------------

// Extracts identifier components from a compiler-mangled type name.
// MSVC: ".?AVButton@UI@Widgets@XER@@" -> {"Button","UI","Widgets","XER"}
// Itanium: "N3XER2UI7Widgets6ButtonE" -> {"XER","UI","Widgets","Button"}
std::vector<std::string> DemangleComponents(const char* mangled) {
    std::vector<std::string> out;
    std::string_view s(mangled ? mangled : "");

    if (s.size() >= 4 && s.substr(0, 4) == ".?AV") {
        // MSVC decorated name: components separated by '@', innermost
        // first, terminated by "@@".
        std::size_t pos = 4;
        while (pos < s.size()) {
            const std::size_t at = s.find('@', pos);
            const std::size_t end =
                (at == std::string_view::npos) ? s.size() : at;
            if (end > pos) out.emplace_back(s.substr(pos, end - pos));
            if (at == std::string_view::npos || s.substr(at, 2) == "@@")
                break;
            pos = at + 1;
        }
        return out;
    }

    if (!s.empty() && s.front() == 'N' && s.back() == 'E') {
        // Itanium ABI: N <len> <name> ... E — length-prefixed components.
        std::size_t pos = 1;
        while (pos < s.size() && s[pos] != 'E') {
            std::size_t len = 0;
            while (pos < s.size() &&
                   std::isdigit(static_cast<unsigned char>(s[pos]))) {
                len = len * 10 + static_cast<unsigned>(s[pos] - '0');
                ++pos;
            }
            if (len == 0 || pos + len > s.size()) break;
            out.emplace_back(s.substr(pos, len));
            pos += len;
        }
        if (!out.empty()) return out;
    }

    // Readable form: "class XER::UI::Widgets::Card" (some toolchains
    // return undecorated names). Split on "::" and reverse so the
    // vector stays innermost-first like the other branches.
    {
        std::string_view t = s;
        for (std::string_view p : {"class ", "struct ", "union ", "enum "}) {
            if (t.substr(0, p.size()) == p) { t.remove_prefix(p.size()); break; }
        }
        if (t.find("::") != std::string_view::npos) {
            std::vector<std::string> fwd;
            std::size_t pos = 0;
            while (pos <= t.size()) {
                const std::size_t c = t.find("::", pos);
                const std::size_t end =
                    (c == std::string_view::npos) ? t.size() : c;
                if (end > pos) fwd.emplace_back(t.substr(pos, end - pos));
                if (c == std::string_view::npos) break;
                pos = c + 2;
            }
            for (auto it = fwd.rbegin(); it != fwd.rend(); ++it)
                out.push_back(*it);
            return out;
        }
    }

    // Fallback: the raw name may still carry usable text.
    if (!s.empty()) out.emplace_back(std::string(s));
    return out;
}

} // namespace

namespace internal {

std::vector<std::string> TypeComponents(const Widgets::Widget& w) {
    return DemangleComponents(typeid(w).name());
}

std::string QualifiedName(const Widgets::Widget& w) {
    auto comps = TypeComponents(w);
    if (comps.empty()) return "Widget";
    std::string q;
    for (auto it = comps.rbegin(); it != comps.rend(); ++it) {
        if (!q.empty()) q += "::";
        q += *it;
    }
    return q;
}

std::string ElementTag(const Widgets::Widget& w) {
    auto comps = TypeComponents(w);
    const std::string leaf =
        comps.empty() ? std::string("Widget") : comps.front();
    std::string clean;
    for (char c : leaf) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_') {
            if (clean.empty() && std::isdigit(u)) clean += '_';
            clean += c;
        }
    }
    if (clean.empty() ||
        std::isdigit(static_cast<unsigned char>(clean.front()))) {
        clean = "Widget";
    }
    return clean;
}

std::string DirName(Widgets::TextDirection d) {
    switch (d) {
    case Widgets::TextDirection::Ltr: return "ltr";
    case Widgets::TextDirection::Rtl: return "rtl";
    default: return "inherit";
    }
}

} // namespace internal

namespace {

#ifdef XER_UI_HAS_LIBXML2

void SetProp(xmlNodePtr node, const char* name, const std::string& value) {
    xmlNewProp(node, reinterpret_cast<const xmlChar*>(name),
               reinterpret_cast<const xmlChar*>(value.c_str()));
}

void SetProp(xmlNodePtr node, const char* name, float v) {
    SetProp(node, name, Fmt(v));
}

void SetProp(xmlNodePtr node, const char* name, bool v) {
    // Self-contained: a const char* argument would resolve back to this
    // overload (pointer->bool beats pointer->std::string).
    xmlNewProp(node, reinterpret_cast<const xmlChar*>(name),
               reinterpret_cast<const xmlChar*>(v ? "true" : "false"));
}

// ---------------------------------------------------------------------------
// Tree walk -> libxml2 document
// ---------------------------------------------------------------------------

struct DumpCtx {
    const DebugMapUI::Options& options;
    std::unordered_set<const Widgets::Widget*> visited;
    int nodes = 0;
};

xmlNodePtr DumpNode(const Widgets::Widget& w, int depth, DumpCtx& ctx) {
    if (!ctx.visited.insert(&w).second) {
        // Defensive: a widget reachable twice (corrupted child list)
        // would otherwise recurse forever.
        xmlNodePtr dup = xmlNewNode(nullptr, BAD_CAST "Widget");
        if (dup != nullptr) {
            xmlNewProp(dup, BAD_CAST "duplicate", BAD_CAST "true");
            xmlNewProp(dup, BAD_CAST "type",
                       BAD_CAST internal::QualifiedName(w).c_str());
        }
        return dup;
    }
    ++ctx.nodes;

    const Layout::Rect b = w.GetBounds();  // absolute client coords
    const auto& children = w.GetChildren();
    const bool truncated =
        !children.empty() && depth >= ctx.options.max_depth;

    xmlNodePtr node =
        xmlNewNode(nullptr, BAD_CAST internal::ElementTag(w).c_str());
    if (node == nullptr) return nullptr;

    SetProp(node, "type", internal::QualifiedName(w));
    SetProp(node, "x", b.x);
    SetProp(node, "y", b.y);
    SetProp(node, "w", b.width);
    SetProp(node, "h", b.height);
    SetProp(node, "visible", w.IsVisible());
    SetProp(node, "shown", w.IsEffectivelyVisible());
    SetProp(node, "enabled", w.IsEnabled());
    SetProp(node, "focused", w.IsFocused());
    SetProp(node, "hovered", w.IsHovered());
    SetProp(node, "focusable", w.IsFocusable());
    if (w.ClipsChildren()) SetProp(node, "clips", true);
    SetProp(node, "dir", internal::DirName(w.EffectiveTextDirection()));
    if (!w.GetTooltip().empty()) {
        SetProp(node, "tooltip", w.GetTooltip());
    }
    const std::string debug_text = w.GetDebugText();
    if (!debug_text.empty()) {
        SetProp(node, "text", debug_text);
    }
    const float content_h = w.GetContentHeight();
    if (content_h > 0.0f) SetProp(node, "contentH", content_h);
    if (w.GetTree() != nullptr) SetProp(node, "inTree", true);
    if (!children.empty()) {
        SetProp(node, "kids", Fmt(static_cast<float>(children.size())));
    }
    if (truncated) SetProp(node, "truncated", true);

    if (children.empty() || truncated) return node;

    for (const auto& child : children) {
        if (!child) continue;
        const bool hidden = !child->IsEffectivelyVisible();
        if (hidden && !ctx.options.include_hidden) {
            xmlNodePtr marker = xmlNewNode(nullptr, BAD_CAST "Widget");
            if (marker != nullptr) {
                xmlNewProp(marker, BAD_CAST "hidden", BAD_CAST "true");
                xmlNewProp(marker, BAD_CAST "type",
                           BAD_CAST internal::QualifiedName(*child).c_str());
                xmlAddChild(node, marker);
            }
            continue;
        }
        if (xmlNodePtr cn = DumpNode(*child, depth + 1, ctx)) {
            xmlAddChild(node, cn);
        }
    }
    return node;
}

#endif // XER_UI_HAS_LIBXML2

} // namespace

std::string DebugMapUI::ToXml(const Widgets::Widget* override_root,
                              const Options& options) const {
    const Widgets::Widget* root = override_root ? override_root : root_;
#ifndef XER_UI_HAS_LIBXML2
    (void)options;
    return root == nullptr
               ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<DebugMapUI nodes=\"0\"/>\n"
               : UnavailableXml();
#else
    if (root == nullptr) {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<DebugMapUI nodes=\"0\"/>\n";
    }
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    if (doc == nullptr) return {};
    xmlNodePtr wrapper = xmlNewNode(nullptr, BAD_CAST "DebugMapUI");
    xmlDocSetRootElement(doc, wrapper);
    DumpCtx ctx{options, {}, 0};
    if (xmlNodePtr body = DumpNode(*root, 0, ctx)) {
        xmlAddChild(wrapper, body);
    }
    xmlSetProp(wrapper, BAD_CAST "nodes",
               BAD_CAST std::to_string(ctx.nodes).c_str());

    xmlChar* buf = nullptr;
    int size = 0;
    xmlDocDumpFormatMemory(doc, &buf, &size, 1);
    std::string out;
    if (buf != nullptr) {
        out.assign(reinterpret_cast<const char*>(buf),
                   static_cast<std::size_t>(size));
        xmlFree(buf);
    }
    xmlFreeDoc(doc);
    return out;
#endif
}

std::string DebugMapUI::Dump(const Widgets::Widget* root,
                             const Options& options) {
    return DebugMapUI(root).ToXml(nullptr, options);
}

} // namespace XER::UI::Debug
