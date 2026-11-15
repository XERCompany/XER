#include "xml_ui.h"

#include "../widgets/widget.h"
#include "../layout/geometry.h"

#ifdef XER_UI_HAS_LIBXML2
#include <libxml/parser.h>
#include <libxml/tree.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <format>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Widget headers
#include "../widgets/accordion.h"
#include "../widgets/alert.h"
#include "../widgets/aspect_ratio.h"
#include "../widgets/attachment.h"
#include "../widgets/avatar.h"
#include "../widgets/badge.h"
#include "../widgets/bubble.h"
#include "../widgets/button.h"
#include "../widgets/button_group.h"
#include "../widgets/calendar.h"
#include "../widgets/card.h"
#include "../widgets/carousel.h"
#include "../widgets/chart.h"
#include "../widgets/checkbox.h"
#include "../widgets/collapsible.h"
#include "../widgets/command.h"
#include "../widgets/context_menu.h"
#include "../widgets/dialog.h"
#include "../widgets/direction.h"
#include "../widgets/drawer.h"
#include "../widgets/dropdown_menu.h"
#include "../widgets/empty.h"
#include "../widgets/field.h"
#include "../widgets/form.h"
#include "../widgets/hover_card.h"
#include "../widgets/input_group.h"
#include "../widgets/item.h"
#include "../widgets/kbd.h"
#include "../widgets/label.h"
#include "../widgets/marker.h"
#include "../widgets/message.h"
#include "../widgets/message_scroller.h"
#include "../widgets/pagination.h"
#include "../widgets/popover.h"
#include "../widgets/progress.h"
#include "../widgets/scroll_area.h"
#include "../widgets/select.h"
#include "../widgets/separator.h"
#include "../widgets/sheet.h"
#include "../widgets/sidebar.h"
#include "../widgets/skeleton.h"
#include "../widgets/slider.h"
#include "../widgets/spinner.h"
#include "../widgets/switch.h"
#include "../widgets/table.h"
#include "../widgets/tabs.h"
#include "../widgets/text_input.h"
#include "../widgets/textarea.h"
#include "../widgets/toggle.h"

namespace XER::UI::Xml {
namespace {

namespace Widgets = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;

#ifndef XER_UI_HAS_LIBXML2
LoadResult Unavailable() {
    LoadResult r;
    r.error = "XmlUi requires libxml2 (XER_UI_HAS_LIBXML2)";
    return r;
}
#endif

// ---------------------------------------------------------------------------
// Attribute helpers
// ---------------------------------------------------------------------------

#ifdef XER_UI_HAS_LIBXML2

bool HasProp(xmlNodePtr node, const char* name) {
    return node != nullptr && xmlHasProp(node, BAD_CAST name) != 0;
}

std::string PropS(xmlNodePtr node, const char* name,
                  const std::string& fallback = {}) {
    if (!HasProp(node, name)) return fallback;
    xmlChar* v = xmlGetProp(node, BAD_CAST name);
    std::string out = v ? reinterpret_cast<const char*>(v) : "";
    if (v) xmlFree(v);
    return out;
}

const char* TagName(xmlNodePtr node) {
    return (node != nullptr && node->name != nullptr)
               ? reinterpret_cast<const char*>(node->name)
               : "";
}

struct BuildCtx {
    std::vector<std::string>* warnings;
    int depth = 0;
    static constexpr int kMaxDepth = 200;
};

void Warn(BuildCtx& ctx, xmlNodePtr node, std::string_view msg) {
    if (ctx.warnings == nullptr) return;
    const long line =
        (node != nullptr) ? xmlGetLineNo(node) : 0;
    ctx.warnings->push_back(
        line > 0 ? std::format("line {}: {}", line, msg)
                 : std::string(msg));
}

bool Truthy(const std::string& v) {
    std::string s;
    s.reserve(v.size());
    for (char c : v) s += static_cast<char>(std::tolower(
        static_cast<unsigned char>(c)));
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

float PropF(xmlNodePtr n, const char* name, float def, BuildCtx& ctx) {
    if (!HasProp(n, name)) return def;
    const std::string s = PropS(n, name);
    try {
        return std::stof(s);
    } catch (...) {
        Warn(ctx, n, std::format("attribute '{}': bad float '{}'",
                                 name, s));
        return def;
    }
}

int PropI(xmlNodePtr n, const char* name, int def, BuildCtx& ctx) {
    if (!HasProp(n, name)) return def;
    const std::string s = PropS(n, name);
    try {
        return std::stoi(s);
    } catch (...) {
        Warn(ctx, n, std::format("attribute '{}': bad int '{}'",
                                 name, s));
        return def;
    }
}

bool PropB(xmlNodePtr n, const char* name, bool def, BuildCtx& ctx) {
    if (!HasProp(n, name)) return def;
    const std::string s = PropS(n, name);
    const std::string t = [&] {
        std::string o;
        for (char c : s)
            o += static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        return o;
    }();
    if (t == "0" || t == "false" || t == "no" || t == "off") return false;
    if (t == "1" || t == "true" || t == "yes" || t == "on") return true;
    Warn(ctx, n, std::format("attribute '{}': bad bool '{}'",
                             name, s));
    return def;
}

std::vector<std::string> PropList(xmlNodePtr n, const char* name,
                                   char sep = ',') {
    std::vector<std::string> out;
    const std::string s = PropS(n, name);
    std::size_t pos = 0;
    while (pos <= s.size()) {
        const std::size_t c = s.find(sep, pos);
        const std::size_t end = (c == std::string::npos) ? s.size() : c;
        std::string item = s.substr(pos, end - pos);
        const auto b = item.find_first_not_of(" \t");
        const auto e = item.find_last_not_of(" \t");
        if (b != std::string::npos) {
            out.push_back(item.substr(b, e - b + 1));
        }
        if (c == std::string::npos) break;
        pos = c + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Common attributes — applied to every widget element after its own attrs.
// ---------------------------------------------------------------------------

Widgets::TextDirection DirOf(const std::string& s, BuildCtx& ctx,
                             xmlNodePtr node) {
    if (s == "ltr") return Widgets::TextDirection::Ltr;
    if (s == "rtl") return Widgets::TextDirection::Rtl;
    if (s == "inherit") return Widgets::TextDirection::Inherit;
    Warn(ctx, node, std::format("attribute 'dir': unknown value '{}'", s));
    return Widgets::TextDirection::Inherit;
}

void ApplyCommon(Widgets::Widget& w, xmlNodePtr node, BuildCtx& ctx) {
    const float x = PropF(node, "x", std::numeric_limits<float>::quiet_NaN(), ctx);
    const float y = PropF(node, "y", std::numeric_limits<float>::quiet_NaN(), ctx);
    const float ww = PropF(node, "width", PropF(node, "w",
        std::numeric_limits<float>::quiet_NaN(), ctx), ctx);
    const float hh = PropF(node, "height", PropF(node, "h",
        std::numeric_limits<float>::quiet_NaN(), ctx), ctx);
    if (!std::isnan(x) || !std::isnan(y) || !std::isnan(ww) ||
        !std::isnan(hh)) {
        Layout::Rect b = w.GetBounds();
        if (!std::isnan(x)) b.x = x;
        if (!std::isnan(y)) b.y = y;
        if (!std::isnan(ww)) b.width = ww;
        if (!std::isnan(hh)) b.height = hh;
        w.SetBounds(b);
    }
    if (HasProp(node, "visible"))
        w.SetVisible(PropB(node, "visible", true, ctx));
    if (HasProp(node, "enabled"))
        w.SetEnabled(PropB(node, "enabled", true, ctx));
    if (HasProp(node, "tooltip"))
        w.SetTooltip(PropS(node, "tooltip"));
    if (HasProp(node, "dir"))
        w.SetTextDirection(DirOf(PropS(node, "dir"), ctx, node));
}

// ---------------------------------------------------------------------------
// Children builders
// ---------------------------------------------------------------------------

std::shared_ptr<Widgets::Widget> BuildElement(xmlNodePtr node,
                                              BuildCtx& ctx);

void BuildChildrenDefault(Widgets::Widget& parent, xmlNodePtr node,
                          BuildCtx& ctx) {
    for (xmlNodePtr c = node->children; c != nullptr; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (auto child = BuildElement(c, ctx)) {
            parent.AddChild(child);
        }
    }
}

// First element child built as a widget (used by single-content
// containers). Returns nullptr when absent.
std::shared_ptr<Widgets::Widget> FirstContent(xmlNodePtr node,
                                              BuildCtx& ctx) {
    for (xmlNodePtr c = node->children; c != nullptr; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        return BuildElement(c, ctx);
    }
    return nullptr;
}

// Remaining element children after the first (for warnings).
int ExtraContentCount(xmlNodePtr node) {
    int n = 0;
    bool first = true;
    for (xmlNodePtr c = node->children; c != nullptr; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (first) { first = false; continue; }
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Widget factories
// ---------------------------------------------------------------------------

using BuildFn = std::function<std::shared_ptr<Widgets::Widget>(
    xmlNodePtr, BuildCtx&)>;

// Wraps a "create + configure attrs" lambda into a full builder that then
// applies common attributes and default child composition.
template <typename Make>
BuildFn Simple(Make&& make) {
    return [make = std::forward<Make>(make)](xmlNodePtr node,
                                             BuildCtx& ctx) {
        auto w = make(node, ctx);
        if (!w) return w;
        ApplyCommon(*w, node, ctx);
        BuildChildrenDefault(*w, node, ctx);
        return w;
    };
}

enum class BtnStyleMap { Primary, Secondary, Outline, Ghost, Destructive };

Widgets::ButtonStyle ButtonStyleOf(const std::string& s) {
    if (s == "primary") return Widgets::ButtonStyle::Primary;
    if (s == "outline") return Widgets::ButtonStyle::Outline;
    if (s == "ghost") return Widgets::ButtonStyle::Ghost;
    if (s == "destructive") return Widgets::ButtonStyle::Destructive;
    return Widgets::ButtonStyle::Secondary;
}

Widgets::BadgeVariant BadgeVariantOf(const std::string& s) {
    if (s == "secondary") return Widgets::BadgeVariant::Secondary;
    if (s == "outline") return Widgets::BadgeVariant::Outline;
    if (s == "accent") return Widgets::BadgeVariant::Accent;
    return Widgets::BadgeVariant::Default;
}

Widgets::Alert::Variant AlertVariantOf(const std::string& s) {
    return s == "destructive" ? Widgets::Alert::Variant::Destructive
                            : Widgets::Alert::Variant::Default;
}

Widgets::Skeleton::Variant SkeletonVariantOf(const std::string& s) {
    return s == "text" ? Widgets::Skeleton::Variant::Text
                       : Widgets::Skeleton::Variant::Block;
}

Widgets::Orientation SepOrientationOf(const std::string& s) {
    return s == "vertical" ? Widgets::Orientation::Vertical
                           : Widgets::Orientation::Horizontal;
}

Widgets::ButtonGroup::Orientation GroupOrientationOf(
    const std::string& s) {
    return s == "vertical"
               ? Widgets::ButtonGroup::Orientation::Vertical
               : Widgets::ButtonGroup::Orientation::Horizontal;
}

Widgets::Bubble::Align BubbleAlignOf(const std::string& s) {
    return s == "own" || s == "self" ? Widgets::Bubble::Align::Own
                                     : Widgets::Bubble::Align::Other;
}

Widgets::Popover::Side PopoverSideOf(const std::string& s) {
    if (s == "top") return Widgets::Popover::Side::Top;
    if (s == "left") return Widgets::Popover::Side::Left;
    if (s == "right") return Widgets::Popover::Side::Right;
    return Widgets::Popover::Side::Bottom;
}

Widgets::Popover::Align PopoverAlignOf(const std::string& s) {
    if (s == "start") return Widgets::Popover::Align::Start;
    if (s == "end") return Widgets::Popover::Align::End;
    return Widgets::Popover::Align::Center;
}

Widgets::Sheet::Side SheetSideOf(const std::string& s) {
    if (s == "left") return Widgets::Sheet::Side::Left;
    if (s == "top") return Widgets::Sheet::Side::Top;
    if (s == "bottom") return Widgets::Sheet::Side::Bottom;
    return Widgets::Sheet::Side::Right;
}

Widgets::Chart::Type ChartTypeOf(const std::string& s) {
    if (s == "area") return Widgets::Chart::Type::Area;
    if (s == "bar") return Widgets::Chart::Type::Bar;
    return Widgets::Chart::Type::Line;
}

Widgets::Tabs::Style TabsStyleOf(const std::string& s) {
    return s == "underline" ? Widgets::Tabs::Style::Underline
                            : Widgets::Tabs::Style::Pill;
}

std::unordered_map<std::string, BuildFn>& Registry() {
    static std::unordered_map<std::string, BuildFn> r = [] {
        std::unordered_map<std::string, BuildFn> m;

        // -- generic containers -------------------------------------------------
        m["Card"] = Simple([](xmlNodePtr, BuildCtx&) {
            return std::shared_ptr<Widgets::Widget>(
                std::make_shared<Widgets::Card>());
        });
        // "Widget" mirrors the JS addon, where UI.Widget is a Card.
        m["Widget"] = m["Card"];

        m["ScrollArea"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto content = FirstContent(n, ctx);
            if (!content) {
                Warn(ctx, n, "ScrollArea expects one content child");
                content = std::make_shared<Widgets::Card>();
            } else if (ExtraContentCount(n) > 0) {
                Warn(ctx, n,
                     "ScrollArea ignores children after the first");
            }
            auto sa = std::make_shared<Widgets::ScrollArea>(content);
            ApplyCommon(*sa, n, ctx);
            return std::shared_ptr<Widgets::Widget>(sa);
        };

        m["Form"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto f = std::make_shared<Widgets::Form>(
                PropI(n, "columns", 1, ctx),
                HasProp(n, "gap") ? PropF(n, "gap", -1.0f, ctx) : -1.0f);
            return std::shared_ptr<Widgets::Widget>(f);
        });

        m["ButtonGroup"] = Simple([](xmlNodePtr n, BuildCtx&) {
            auto g = std::make_shared<Widgets::ButtonGroup>(
                GroupOrientationOf(PropS(n, "orientation")));
            return std::shared_ptr<Widgets::Widget>(g);
        });

        m["AspectRatio"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto a = std::make_shared<Widgets::AspectRatio>(
                PropF(n, "ratio", 16.0f / 9.0f, ctx));
            return std::shared_ptr<Widgets::Widget>(a);
        });

        m["Direction"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto d = std::make_shared<Widgets::Direction>(
                DirOf(PropS(n, "direction", "inherit"), ctx, n));
            return std::shared_ptr<Widgets::Widget>(d);
        });

        m["Collapsible"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto c = std::make_shared<Widgets::Collapsible>(
                PropB(n, "open", false, ctx));
            return std::shared_ptr<Widgets::Widget>(c);
        });

        m["Field"] = Simple([](xmlNodePtr n, BuildCtx&) {
            auto f = std::make_shared<Widgets::Field>();
            if (HasProp(n, "label")) f->SetLabel(PropS(n, "label"));
            if (HasProp(n, "description"))
                f->SetDescription(PropS(n, "description"));
            if (HasProp(n, "error")) f->SetError(PropS(n, "error"));
            return std::shared_ptr<Widgets::Widget>(f);
        });

        m["InputGroup"] = Simple([](xmlNodePtr n, BuildCtx&) {
            auto g = std::make_shared<Widgets::InputGroup>();
            if (HasProp(n, "prefix")) g->SetPrefix(PropS(n, "prefix"));
            if (HasProp(n, "suffix")) g->SetSuffix(PropS(n, "suffix"));
            return std::shared_ptr<Widgets::Widget>(g);
        });

        // -- text / display ------------------------------------------------------
        m["Label"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto l = std::make_shared<Widgets::Label>(
                PropS(n, "text"), PropB(n, "muted", false, ctx));
            return std::shared_ptr<Widgets::Widget>(l);
        });

        m["Button"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto b = std::make_shared<Widgets::Button>(
                PropS(n, "text"),
                ButtonStyleOf(PropS(n, "style", "secondary")));
            if (HasProp(n, "loading"))
                b->SetLoading(PropB(n, "loading", false, ctx));
            return std::shared_ptr<Widgets::Widget>(b);
        });

        m["Badge"] = Simple([](xmlNodePtr n, BuildCtx&) {
            auto b = std::make_shared<Widgets::Badge>(
                PropS(n, "text"),
                BadgeVariantOf(PropS(n, "variant", "default")));
            return std::shared_ptr<Widgets::Widget>(b);
        });

        m["Kbd"] = Simple([](xmlNodePtr n, BuildCtx&) {
            return std::shared_ptr<Widgets::Widget>(
                std::make_shared<Widgets::Kbd>(PropS(n, "text")));
        });

        m["Marker"] = Simple([](xmlNodePtr n, BuildCtx&) {
            return std::shared_ptr<Widgets::Widget>(
                std::make_shared<Widgets::Marker>(PropS(n, "text")));
        });

        m["Avatar"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto a = std::make_shared<Widgets::Avatar>(
                PropS(n, "name"), PropF(n, "size", 40.0f, ctx));
            return std::shared_ptr<Widgets::Widget>(a);
        });

        m["Alert"] = Simple([](xmlNodePtr n, BuildCtx&) {
            auto a = std::make_shared<Widgets::Alert>(
                PropS(n, "title"), PropS(n, "description"),
                AlertVariantOf(PropS(n, "variant", "default")));
            return std::shared_ptr<Widgets::Widget>(a);
        });

        m["Empty"] = Simple([](xmlNodePtr n, BuildCtx&) {
            auto e = std::make_shared<Widgets::Empty>();
            if (HasProp(n, "title")) e->SetTitle(PropS(n, "title"));
            if (HasProp(n, "description"))
                e->SetDescription(PropS(n, "description"));
            return std::shared_ptr<Widgets::Widget>(e);
        });

        m["Item"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto i = std::make_shared<Widgets::Item>();
            if (HasProp(n, "title")) i->SetTitle(PropS(n, "title"));
            if (HasProp(n, "description"))
                i->SetDescription(PropS(n, "description"));
            if (HasProp(n, "selected"))
                i->SetSelected(PropB(n, "selected", false, ctx));
            return std::shared_ptr<Widgets::Widget>(i);
        });

        m["Bubble"] = Simple([](xmlNodePtr n, BuildCtx&) {
            auto b = std::make_shared<Widgets::Bubble>(
                PropS(n, "text"),
                BubbleAlignOf(PropS(n, "align", "other")));
            return std::shared_ptr<Widgets::Widget>(b);
        });

        m["Attachment"] = Simple([](xmlNodePtr n, BuildCtx&) {
            auto a = std::make_shared<Widgets::Attachment>(
                PropS(n, "filename"), PropS(n, "meta"));
            return std::shared_ptr<Widgets::Widget>(a);
        });

        m["Message"] = Simple([](xmlNodePtr n, BuildCtx&) {
            auto m = std::make_shared<Widgets::Message>(
                PropS(n, "sender"), PropS(n, "text"),
                PropS(n, "time"),
                BubbleAlignOf(PropS(n, "align", "other")));
            return std::shared_ptr<Widgets::Widget>(m);
        });

        // -- inputs ---------------------------------------------------------------
        m["Checkbox"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            return std::shared_ptr<Widgets::Widget>(
                std::make_shared<Widgets::Checkbox>(
                    PropB(n, "checked", false, ctx)));
        });

        m["Switch"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            return std::shared_ptr<Widgets::Widget>(
                std::make_shared<Widgets::Switch>(
                    PropB(n, "checked", false, ctx)));
        });

        m["Toggle"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto t = std::make_shared<Widgets::Toggle>(
                PropS(n, "text"), PropB(n, "pressed", false, ctx));
            return std::shared_ptr<Widgets::Widget>(t);
        });

        m["TextInput"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto t = std::make_shared<Widgets::TextInput>(
                PropS(n, "value", PropS(n, "text")));
            if (HasProp(n, "placeholder"))
                t->SetPlaceholder(PropS(n, "placeholder"));
            if (HasProp(n, "password"))
                t->SetPasswordMode(PropB(n, "password", false, ctx));
            return std::shared_ptr<Widgets::Widget>(t);
        });

        m["Textarea"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto t = std::make_shared<Widgets::Textarea>(
                PropS(n, "text", PropS(n, "value")));
            if (HasProp(n, "placeholder"))
                t->SetPlaceholder(PropS(n, "placeholder"));
            if (HasProp(n, "readonly"))
                t->SetReadOnly(PropB(n, "readonly", false, ctx));
            return std::shared_ptr<Widgets::Widget>(t);
        });

        m["Slider"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto s = std::make_shared<Widgets::Slider>(
                PropF(n, "min", 0.0f, ctx), PropF(n, "max", 1.0f, ctx),
                PropF(n, "value", 0.0f, ctx));
            return std::shared_ptr<Widgets::Widget>(s);
        });

        m["Progress"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            return std::shared_ptr<Widgets::Widget>(
                std::make_shared<Widgets::Progress>(
                    PropF(n, "value", 0.0f, ctx)));
        });

        m["Select"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto s = std::make_shared<Widgets::Select>(
                PropList(n, "options"), PropI(n, "selected", -1, ctx));
            if (HasProp(n, "placeholder"))
                s->SetPlaceholder(PropS(n, "placeholder"));
            return std::shared_ptr<Widgets::Widget>(s);
        });

        // -- feedback / misc -------------------------------------------------------
        m["Spinner"] = Simple([](xmlNodePtr, BuildCtx&) {
            return std::shared_ptr<Widgets::Widget>(
                std::make_shared<Widgets::Spinner>());
        });

        m["Skeleton"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto s = std::make_shared<Widgets::Skeleton>(
                PropF(n, "width", 0.0f, ctx), PropF(n, "height", 0.0f, ctx),
                SkeletonVariantOf(PropS(n, "variant", "block")));
            return std::shared_ptr<Widgets::Widget>(s);
        });

        m["Pagination"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto p = std::make_shared<Widgets::Pagination>(
                PropI(n, "pages", 1, ctx), PropI(n, "page", 0, ctx));
            return std::shared_ptr<Widgets::Widget>(p);
        });

        m["Separator"] = Simple([](xmlNodePtr n, BuildCtx&) {
            return std::shared_ptr<Widgets::Widget>(
                std::make_shared<Widgets::Separator>(
                    SepOrientationOf(PropS(n, "orientation", "horizontal"))));
        });

        m["Calendar"] = Simple([](xmlNodePtr n, BuildCtx& ctx) {
            auto c = Widgets::Calendar::Create();
            if (HasProp(n, "date")) {
                // YYYY-MM-DD (also accepts YYYY/MM/DD and YYYY.MM.DD)
                const std::string d = PropS(n, "date");
                std::vector<std::string> parts;
                for (char sep : {'-', '/', '.'}) {
                    if (d.find(sep) != std::string::npos) {
                        std::size_t pos = 0;
                        while (pos <= d.size()) {
                            const std::size_t cpos = d.find(sep, pos);
                            const std::size_t end =
                                (cpos == std::string::npos) ? d.size() : cpos;
                            parts.push_back(d.substr(pos, end - pos));
                            if (cpos == std::string::npos) break;
                            pos = cpos + 1;
                        }
                        break;
                    }
                }
                bool ok = parts.size() == 3;
                int y = 0, mo = 0, da = 0;
                if (ok) {
                    try {
                        y = std::stoi(parts[0]);
                        mo = std::stoi(parts[1]);
                        da = std::stoi(parts[2]);
                    } catch (...) { ok = false; }
                }
                if (ok) {
                    c->SetDate({y, mo, da});
                } else {
                    Warn(ctx, n,
                         std::format("attribute 'date': bad value '{}'",
                                     d));
                }
            }
            return std::shared_ptr<Widgets::Widget>(c);
        });

        // -- special containers ----------------------------------------------------

        m["Tabs"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto t = std::make_shared<Widgets::Tabs>();
            t->SetStyle(TabsStyleOf(PropS(n, "style", "pill")));
            int active = PropI(n, "active", -1, ctx);
            int tab_index = 0;
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                if (std::string(TagName(c)) != "Tab") {
                    Warn(ctx, c, std::format(
                             "Tabs only accepts <Tab> children, got <{}>",
                             TagName(c)));
                    continue;
                }
                const std::string label = PropS(c, "label");
                t->AddTab(label);
                if (active < 0 &&
                    PropB(c, "active", false, ctx)) {
                    active = tab_index;
                }
                ++tab_index;
                // Tab content panes are not supported by Widgets::Tabs.
                for (xmlNodePtr gc = c->children; gc != nullptr;
                     gc = gc->next) {
                    if (gc->type == XML_ELEMENT_NODE) {
                        Warn(ctx, gc,
                             "Tabs do not host per-tab content yet");
                        break;
                    }
                }
            }
            if (active >= 0) t->SetActiveIndex(active);
            ApplyCommon(*t, n, ctx);
            return std::shared_ptr<Widgets::Widget>(t);
        };

        m["Accordion"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto a = std::make_shared<Widgets::Accordion>();
            std::vector<Widgets::Accordion::Item> items;
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                if (std::string(TagName(c)) != "Item") {
                    Warn(ctx, c, std::format(
                             "Accordion only accepts <Item> children, "
                             "got <{}>", TagName(c)));
                    continue;
                }
                Widgets::Accordion::Item it;
                it.title = PropS(c, "title");
                it.open = PropB(c, "open", false, ctx);
                it.content = FirstContent(c, ctx);
                if (!it.content) {
                    Warn(ctx, c, "Accordion <Item> has no content child");
                    it.content = std::make_shared<Widgets::Card>();
                } else if (ExtraContentCount(c) > 0) {
                    Warn(ctx, c,
                         "Accordion <Item> ignores children after the "
                         "first");
                }
                items.push_back(std::move(it));
            }
            a->SetItems(std::move(items),
                        PropB(n, "single_open", true, ctx));
            ApplyCommon(*a, n, ctx);
            return std::shared_ptr<Widgets::Widget>(a);
        };

        m["Carousel"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto c = Widgets::Carousel::Create();
            for (xmlNodePtr ch = n->children; ch != nullptr;
                 ch = ch->next) {
                if (ch->type != XML_ELEMENT_NODE) continue;
                if (auto slide = BuildElement(ch, ctx)) {
                    c->AddSlide(slide);
                }
            }
            ApplyCommon(*c, n, ctx);
            return std::shared_ptr<Widgets::Widget>(c);
        };

        m["Dialog"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto content = FirstContent(n, ctx);
            if (ExtraContentCount(n) > 0) {
                Warn(ctx, n, "Dialog ignores children after the first");
            }
            auto d = Widgets::Dialog::Create(
                PropS(n, "title"), content,
                PropS(n, "confirm", "OK"), PropS(n, "cancel"));
            ApplyCommon(*d, n, ctx);
            return std::shared_ptr<Widgets::Widget>(d);
        };

        m["Drawer"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto content = FirstContent(n, ctx);
            if (ExtraContentCount(n) > 0) {
                Warn(ctx, n, "Drawer ignores children after the first");
            }
            auto d = Widgets::Drawer::Create(PropS(n, "title"), content);
            if (HasProp(n, "snap"))
                d->SetSnapPoint(PropF(n, "snap", 0.5f, ctx));
            ApplyCommon(*d, n, ctx);
            return std::shared_ptr<Widgets::Widget>(d);
        };

        m["Sheet"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto content = FirstContent(n, ctx);
            if (ExtraContentCount(n) > 0) {
                Warn(ctx, n, "Sheet ignores children after the first");
            }
            auto s = Widgets::Sheet::Create(
                PropS(n, "title"), content,
                SheetSideOf(PropS(n, "side", "right")));
            if (HasProp(n, "size")) s->SetPanelSize(PropF(n, "size", 0.0f, ctx));
            ApplyCommon(*s, n, ctx);
            return std::shared_ptr<Widgets::Widget>(s);
        };

        m["HoverCard"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto h = std::make_shared<Widgets::HoverCard>();
            std::shared_ptr<Widgets::Widget> trigger, content;
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                if (auto w = BuildElement(c, ctx)) {
                    if (!trigger) trigger = w;
                    else if (!content) content = w;
                    else Warn(ctx, c,
                              "HoverCard ignores children after trigger "
                              "and content");
                }
            }
            h->SetTrigger(trigger);
            h->SetContent(content);
            if (HasProp(n, "title")) h->SetTitle(PropS(n, "title"));
            if (HasProp(n, "description"))
                h->SetDescription(PropS(n, "description"));
            if (HasProp(n, "delay"))
                h->SetOpenDelay(PropF(n, "delay", 0.0f, ctx));
            if (HasProp(n, "action"))
                h->SetActionLabel(PropS(n, "action"));
            ApplyCommon(*h, n, ctx);
            return std::shared_ptr<Widgets::Widget>(h);
        };

        m["Popover"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto p = std::make_shared<Widgets::Popover>();
            std::shared_ptr<Widgets::Widget> trigger, content;
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                const std::string tag = TagName(c);
                if (tag == "Trigger") {
                    trigger = FirstContent(c, ctx);
                    continue;
                }
                if (tag == "Content") {
                    content = FirstContent(c, ctx);
                    continue;
                }
                if (auto w = BuildElement(c, ctx)) {
                    if (!trigger) trigger = w;
                    else if (!content) content = w;
                    else Warn(ctx, c,
                              "Popover ignores children after trigger "
                              "and content");
                }
            }
            p->SetTrigger(trigger);
            p->SetContent(content);
            if (HasProp(n, "title")) p->SetTitle(PropS(n, "title"));
            p->SetSide(PopoverSideOf(PropS(n, "side", "bottom")));
            p->SetAlign(PopoverAlignOf(PropS(n, "align", "center")));
            if (HasProp(n, "open")) p->SetOpen(PropB(n, "open", false, ctx));
            ApplyCommon(*p, n, ctx);
            return std::shared_ptr<Widgets::Widget>(p);
        };

        // -- menus -----------------------------------------------------------------

        auto menu_items = [](xmlNodePtr n, BuildCtx& ctx,
                             std::vector<Widgets::ContextMenu::Item>& out,
                             bool with_shortcut) {
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                if (std::string(TagName(c)) != "Item") {
                    Warn(ctx, c, std::format(
                             "menu only accepts <Item> children, got <{}>",
                             TagName(c)));
                    continue;
                }
                Widgets::ContextMenu::Item it;
                it.label = PropS(c, "label", PropS(c, "text"));
                it.enabled = PropB(c, "enabled", true, ctx);
                it.checked = PropB(c, "checked", false, ctx);
                it.separator_before =
                    PropB(c, "separator", false, ctx);
                it.danger = PropB(c, "danger", false, ctx);
                if (with_shortcut && HasProp(c, "shortcut"))
                    it.shortcut = PropS(c, "shortcut");
                out.push_back(std::move(it));
            }
        };

        m["ContextMenu"] = [menu_items](xmlNodePtr n, BuildCtx& ctx) {
            std::vector<Widgets::ContextMenu::Item> items;
            menu_items(n, ctx, items, true);
            auto cm = Widgets::ContextMenu::Create(std::move(items));
            ApplyCommon(*cm, n, ctx);
            return std::shared_ptr<Widgets::Widget>(cm);
        };

        m["DropdownMenu"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto dm = std::make_shared<Widgets::DropdownMenu>();
            std::vector<Widgets::ContextMenu::Item> items;
            std::shared_ptr<Widgets::Widget> trigger;
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                const std::string tag = TagName(c);
                if (tag == "Item") {
                    // handled below via menu_items pass — collect here
                    continue;
                }
                if (tag == "Trigger" || tag == "Content") {
                    if (auto w = FirstContent(c, ctx)) trigger = w;
                    continue;
                }
                if (auto w = BuildElement(c, ctx)) {
                    if (!trigger) {
                        trigger = w;
                    } else {
                        Warn(ctx, c,
                             "DropdownMenu ignores extra element children");
                    }
                }
            }
            // Second pass for <Item> pseudo-elements.
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                if (std::string(TagName(c)) != "Item") continue;
                Widgets::ContextMenu::Item it;
                it.label = PropS(c, "label", PropS(c, "text"));
                it.enabled = PropB(c, "enabled", true, ctx);
                it.checked = PropB(c, "checked", false, ctx);
                it.separator_before = PropB(c, "separator", false, ctx);
                it.danger = PropB(c, "danger", false, ctx);
                if (HasProp(c, "shortcut")) it.shortcut = PropS(c, "shortcut");
                items.push_back(std::move(it));
            }
            dm->SetTrigger(trigger);
            dm->SetItems(std::move(items));
            ApplyCommon(*dm, n, ctx);
            return std::shared_ptr<Widgets::Widget>(dm);
        };

        m["Sidebar"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto s = Widgets::Sidebar::Create(PropS(n, "header"));
            std::vector<Widgets::Sidebar::Item> items;
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                if (std::string(TagName(c)) != "Item") {
                    Warn(ctx, c, std::format(
                             "Sidebar only accepts <Item> children, "
                             "got <{}>", TagName(c)));
                    continue;
                }
                Widgets::Sidebar::Item it;
                it.label = PropS(c, "label", PropS(c, "text"));
                it.icon = PropS(c, "icon");
                it.enabled = PropB(c, "enabled", true, ctx);
                it.separator_before = PropB(c, "separator", false, ctx);
                items.push_back(std::move(it));
            }
            s->SetItems(std::move(items));
            if (HasProp(n, "active"))
                s->SetActiveIndex(PropI(n, "active", -1, ctx));
            ApplyCommon(*s, n, ctx);
            return std::shared_ptr<Widgets::Widget>(s);
        };

        m["Command"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto c = Widgets::Command::Create(PropS(n, "placeholder"));
            std::vector<Widgets::Command::Item> items;
            for (xmlNodePtr ch = n->children; ch != nullptr; ch = ch->next) {
                if (ch->type != XML_ELEMENT_NODE) continue;
                if (std::string(TagName(ch)) != "Item") {
                    Warn(ctx, ch, std::format(
                             "Command only accepts <Item> children, "
                             "got <{}>", TagName(ch)));
                    continue;
                }
                Widgets::Command::Item it;
                it.label = PropS(ch, "label", PropS(ch, "text"));
                it.hint = PropS(ch, "hint");
                it.enabled = PropB(ch, "enabled", true, ctx);
                // it.action stays empty — actions are wired in code.
                items.push_back(std::move(it));
            }
            c->SetItems(std::move(items));
            ApplyCommon(*c, n, ctx);
            return std::shared_ptr<Widgets::Widget>(c);
        };

        // -- data widgets ------------------------------------------------------------

        m["Table"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto t = Widgets::Table::Create();
            std::vector<Widgets::Table::Column> cols;
            std::vector<std::vector<std::string>> rows;
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                const std::string tag = TagName(c);
                if (tag == "Column") {
                    Widgets::Table::Column col;
                    col.header = PropS(c, "header", PropS(c, "label"));
                    col.width = PropF(c, "width", 0.0f, ctx);
                    col.sortable = PropB(c, "sortable", false, ctx);
                    cols.push_back(std::move(col));
                } else if (tag == "Row") {
                    std::vector<std::string> row;
                    for (xmlNodePtr cell = c->children; cell != nullptr;
                         cell = cell->next) {
                        if (cell->type != XML_ELEMENT_NODE) continue;
                        if (std::string(TagName(cell)) == "Cell") {
                            row.push_back(PropS(cell, "text",
                                                PropS(cell, "value")));
                        } else {
                            Warn(ctx, cell, std::format(
                                     "Table <Row> only accepts <Cell>, "
                                     "got <{}>", TagName(cell)));
                        }
                    }
                    rows.push_back(std::move(row));
                } else {
                    Warn(ctx, c, std::format(
                             "Table only accepts <Column>/<Row>, got <{}>",
                             tag));
                }
            }
            t->SetColumns(std::move(cols));
            t->SetRows(std::move(rows));
            if (HasProp(n, "header"))
                t->SetHeader(PropB(n, "header", true, ctx));
            ApplyCommon(*t, n, ctx);
            return std::shared_ptr<Widgets::Widget>(t);
        };

        m["Chart"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto ch = Widgets::Chart::Create(
                ChartTypeOf(PropS(n, "type", "line")));
            std::vector<Widgets::Chart::Series> series;
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                if (std::string(TagName(c)) != "Series") {
                    Warn(ctx, c, std::format(
                             "Chart only accepts <Series> children, "
                             "got <{}>", TagName(c)));
                    continue;
                }
                Widgets::Chart::Series s;
                s.label = PropS(c, "label");
                for (const auto& v : PropList(c, "values")) {
                    try {
                        s.values.push_back(std::stof(v));
                    } catch (...) {
                        Warn(ctx, c, std::format(
                                 "Series values: bad float '{}'", v));
                    }
                }
                series.push_back(std::move(s));
            }
            ch->SetSeries(std::move(series));
            if (HasProp(n, "title")) ch->SetTitle(PropS(n, "title"));
            if (HasProp(n, "grid"))
                ch->SetShowGrid(PropB(n, "grid", true, ctx));
            if (HasProp(n, "legend"))
                ch->SetShowLegend(PropB(n, "legend", true, ctx));
            ApplyCommon(*ch, n, ctx);
            return std::shared_ptr<Widgets::Widget>(ch);
        };

        m["MessageScroller"] = [](xmlNodePtr n, BuildCtx& ctx) {
            auto ms = std::make_shared<Widgets::MessageScroller>();
            for (xmlNodePtr c = n->children; c != nullptr; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;
                if (auto w = BuildElement(c, ctx)) {
                    ms->AddMessage(w);
                }
            }
            ApplyCommon(*ms, n, ctx);
            return std::shared_ptr<Widgets::Widget>(ms);
        };

        return m;
    }();
    return r;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

std::shared_ptr<Widgets::Widget> BuildElement(xmlNodePtr node,
                                              BuildCtx& ctx) {
    if (ctx.depth >= BuildCtx::kMaxDepth) {
        Warn(ctx, node, "maximum nesting depth exceeded");
        return nullptr;
    }
    const std::string tag = TagName(node);
    auto& reg = Registry();
    const auto it = reg.find(tag);
    if (it == reg.end()) {
        Warn(ctx, node, std::format("unsupported element <{}>", tag));
        return nullptr;
    }
    ++ctx.depth;
    auto w = it->second(node, ctx);
    --ctx.depth;
    return w;
}

LoadResult Finish(xmlNodePtr root_element, BuildCtx& ctx) {
    LoadResult result;
    if (root_element == nullptr ||
        root_element->type != XML_ELEMENT_NODE) {
        result.error = "document has no root element";
        result.warnings = std::move(*ctx.warnings);
        return result;
    }
    result.root = BuildElement(root_element, ctx);
    if (!result.root) {
        result.error = std::format("failed to build <{}>",
                                   TagName(root_element));
    }
    result.warnings = std::move(*ctx.warnings);
    return result;
}

#endif // XER_UI_HAS_LIBXML2

} // namespace

// libxml2 prints parse errors to stderr through the generic error
// channel; the loader reports them via LoadResult instead, so parser
// contexts get no-op SAX error callbacks.
void SilentSaxError(void*, const char*, ...) {}
void SilentSaxWarning(void*, const char*, ...) {}

xmlDocPtr ParseSilent(const char* data, int size) {
    static const bool init = [] {
        xmlInitParser();
        return true;
    }();
    (void)init;
    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();
    if (ctxt == nullptr) return nullptr;
    ctxt->sax->error = SilentSaxError;
    ctxt->sax->warning = SilentSaxWarning;
    xmlCtxtUseOptions(ctxt, XML_PARSE_NONET | XML_PARSE_NOENT |
                             XML_PARSE_HUGE);
    xmlDocPtr doc = xmlCtxtReadMemory(ctxt, data, size, "xer-ui.xml",
                                      nullptr, 0);
    xmlFreeParserCtxt(ctxt);
    return doc;
}

LoadResult Load(std::string_view markup) {
#ifdef XER_UI_HAS_LIBXML2
    LoadResult result;
    std::vector<std::string> warnings;
    BuildCtx ctx{&warnings, 0};

    xmlDocPtr doc = ParseSilent(markup.data(),
                                static_cast<int>(markup.size()));
    if (doc == nullptr) {
        const xmlError* err = xmlGetLastError();
        result.error = err != nullptr && err->message != nullptr
                           ? std::string("XML parse error: ") +
                                 reinterpret_cast<const char*>(err->message)
                           : "XML parse error";
        result.warnings = std::move(warnings);
        return result;
    }
    result = Finish(xmlDocGetRootElement(doc), ctx);
    xmlFreeDoc(doc);
    return result;
#else
    (void)markup;
    return Unavailable();
#endif
}

LoadResult LoadFile(const std::string& utf8_path) {
#ifdef XER_UI_HAS_LIBXML2
    std::ifstream in(utf8_path, std::ios::binary);
    if (!in) {
        LoadResult r;
        r.error = std::format("cannot open '{}'", utf8_path);
        return r;
    }
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    return Load(data);
#else
    (void)utf8_path;
    return Unavailable();
#endif
}

std::string SupportedTags() {
#ifdef XER_UI_HAS_LIBXML2
    std::vector<std::string> tags;
    tags.reserve(Registry().size());
    for (const auto& [tag, _] : Registry()) tags.push_back(tag);
    std::sort(tags.begin(), tags.end());
    std::string out;
    for (const auto& t : tags) {
        if (!out.empty()) out += ", ";
        out += t;
    }
    return out;
#else
    return {};
#endif
}

} // namespace XER::UI::Xml
