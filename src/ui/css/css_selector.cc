#include "ui/css/css_selector.h"

#include <cctype>
#include <cstdlib>

namespace XER::UI::CSS {

namespace {

bool MatchesNth(int a, int b, int index) {
    if (index < 0) return false;
    const int n = index + 1;  // nth is 1-based
    if (a == 0) return n == b;
    const int d = n - b;
    return d % a == 0 && (d / a) >= 0;
}

}  // namespace

bool CompoundSelector::Matches(const CSSElement& el) const {
    for (const auto& s : simples) {
        switch (s.type) {
            case SimpleSelector::Type::kUniversal:
                break;
            case SimpleSelector::Type::kTag:
                if (el.TagName() != s.value) return false;
                break;
            case SimpleSelector::Type::kId:
                if (el.Id() != s.value) return false;
                break;
            case SimpleSelector::Type::kClass:
                if (!el.HasClass(s.value)) return false;
                break;
            case SimpleSelector::Type::kAttribute:
                if (!el.HasAttribute(s.value)) return false;
                if (!s.attr_value.empty() &&
                    el.AttributeValue(s.value) != s.attr_value)
                    return false;
                break;
            case SimpleSelector::Type::kFocus:
                if (!el.IsFocused()) return false;
                break;
            case SimpleSelector::Type::kHover:
                if (!el.IsHovered()) return false;
                break;
            case SimpleSelector::Type::kActive:
                if (!el.IsActive()) return false;
                break;
            case SimpleSelector::Type::kFirstChild:
                if (el.NthChildIndex() != 0) return false;
                break;
            case SimpleSelector::Type::kLastChild: {
                const int idx = el.NthChildIndex();
                if (idx < 0 || el.SiblingCount() < 0 ||
                    idx != el.SiblingCount() - 1)
                    return false;
                break;
            }
            case SimpleSelector::Type::kNthChild:
                if (!MatchesNth(s.nth_a, s.nth_b, el.NthChildIndex()))
                    return false;
                break;
        }
    }
    return true;
}

namespace {

// Parses "an+b" for :nth-child: "odd", "even", "3", "2n+1", "-n+3".
void ParseNth(const std::string& expr, int* a, int* b) {
    *a = 0;
    *b = 0;
    std::string e;
    for (char c : expr)
        if (!std::isspace(static_cast<unsigned char>(c))) e += c;
    if (e == "odd") { *a = 2; *b = 1; return; }
    if (e == "even") { *a = 2; *b = 0; return; }
    const auto npos = e.find('n');
    if (npos == std::string::npos) {
        *b = std::atoi(e.c_str());
        return;
    }
    const std::string astr = e.substr(0, npos);
    *a = astr.empty() || astr == "+" ? 1 : astr == "-" ? -1
                                                      : std::atoi(astr.c_str());
    *b = npos + 1 < e.size() ? std::atoi(e.c_str() + npos + 1) : 0;
}

bool ParsePseudo(CompoundSelector* comp, const std::string& tok,
                 std::size_t* i) {
    using T = SimpleSelector::Type;
    struct P { const char* name; T type; };
    static const P kSimple[] = {
        {":focus", T::kFocus}, {":hover", T::kHover},
        {":active", T::kActive}, {":first-child", T::kFirstChild},
        {":last-child", T::kLastChild},
    };
    for (const auto& p : kSimple) {
        const std::size_t len = std::char_traits<char>::length(p.name);
        if (tok.compare(*i, len, p.name) == 0) {
            comp->simples.push_back({p.type, {}});
            *i += len;
            return true;
        }
    }
    static const char kNth[] = ":nth-child(";
    if (tok.compare(*i, sizeof(kNth) - 1, kNth) == 0) {
        const std::size_t open = *i + sizeof(kNth) - 1;
        const std::size_t close = tok.find(')', open);
        SimpleSelector s;
        s.type = T::kNthChild;
        ParseNth(tok.substr(open, close == std::string::npos
                                       ? close : close - open),
                 &s.nth_a, &s.nth_b);
        comp->simples.push_back(std::move(s));
        *i = close == std::string::npos ? tok.size() : close + 1;
        return true;
    }
    return false;
}

}  // namespace

Selector ParseSelector(const std::string& text) {
    Selector sel;
    // Split on combinators: whitespace = descendant, '>' = child.
    std::vector<std::pair<CompoundSelector::Combinator, std::string>>
        parts;  // left-to-right
    std::string cur;
    auto comb = CompoundSelector::Combinator::kSelf;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : ' ';
        if (c == ' ' || c == '\t' || c == '>') {
            if (!cur.empty()) {
                parts.emplace_back(comb, cur);
                cur.clear();
                comb = CompoundSelector::Combinator::kDescendant;
            }
            if (c == '>' && !parts.empty())
                comb = CompoundSelector::Combinator::kChild;
        } else {
            cur += c;
        }
    }
    // Parse each compound: "*"? tag? then simple-selector suffixes.
    for (auto rit = parts.rbegin(); rit != parts.rend(); ++rit) {
        CompoundSelector comp;
        comp.combinator = rit == parts.rbegin()
                              ? CompoundSelector::Combinator::kSelf
                              : rit->first;
        const std::string& tok = rit->second;
        std::size_t i = 0;
        if (i < tok.size() && tok[i] == '*') {
            comp.simples.push_back({SimpleSelector::Type::kUniversal, {}});
            ++i;
        }
        std::size_t start = i;
        while (start < tok.size() &&
               std::isalpha(static_cast<unsigned char>(tok[start])))
            ++start;
        if (start > i) {
            comp.simples.push_back(
                {SimpleSelector::Type::kTag, tok.substr(i, start - i)});
        }
        i = start;
        while (i < tok.size()) {
            if (tok[i] == '.') {
                std::size_t j = i + 1;
                while (j < tok.size() &&
                       (std::isalnum(static_cast<unsigned char>(tok[j])) ||
                        tok[j] == '_' || tok[j] == '-'))
                    ++j;
                comp.simples.push_back(
                    {SimpleSelector::Type::kClass,
                     tok.substr(i + 1, j - i - 1), {}});
                i = j;
            } else if (tok[i] == '#') {
                std::size_t j = i + 1;
                while (j < tok.size() &&
                       (std::isalnum(static_cast<unsigned char>(tok[j])) ||
                        tok[j] == '_' || tok[j] == '-'))
                    ++j;
                comp.simples.push_back(
                    {SimpleSelector::Type::kId,
                     tok.substr(i + 1, j - i - 1), {}});
                i = j;
            } else if (tok[i] == '[') {
                const std::size_t j = tok.find(']', i);
                std::string body = tok.substr(
                    i + 1, j == std::string::npos ? j : j - i - 1);
                const auto eq = body.find('=');
                SimpleSelector s{SimpleSelector::Type::kAttribute, {}, {}};
                if (eq == std::string::npos) {
                    s.value = std::move(body);
                } else {
                    s.value = body.substr(0, eq);
                    s.attr_value = body.substr(eq + 1);
                    // strip optional quotes around the value
                    if (s.attr_value.size() >= 2 &&
                        (s.attr_value.front() == '"' ||
                         s.attr_value.front() == '\''))
                        s.attr_value = s.attr_value.substr(
                            1, s.attr_value.size() - 2);
                }
                comp.simples.push_back(std::move(s));
                i = j == std::string::npos ? tok.size() : j + 1;
            } else if (tok[i] == ':') {
                if (!ParsePseudo(&comp, tok, &i)) ++i;
            } else {
                ++i;  // skip unrecognized character
            }
        }
        sel.compounds.push_back(std::move(comp));
    }
    return sel;
}

std::vector<Selector> ParseSelectorList(const std::string& text) {
    std::vector<Selector> out;
    std::string cur;
    int depth = 0;
    for (char c : text) {
        if (c == '(' || c == '[') ++depth;
        if (c == ')' || c == ']') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(ParseSelector(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(ParseSelector(cur));
    return out;
}

}  // namespace XER::UI::CSS
