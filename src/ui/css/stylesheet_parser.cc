#include "ui/css/stylesheet_parser.h"

#include <cctype>

namespace XER::UI::CSS {

namespace {

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

// Removes /* ... */ comments.
std::string StripComments(const std::string& css) {
    std::string out;
    for (std::size_t i = 0; i < css.size(); ++i) {
        if (i + 1 < css.size() && css[i] == '/' && css[i + 1] == '*') {
            const std::size_t end = css.find("*/", i + 2);
            i = end == std::string::npos ? css.size() : end + 1;
            continue;
        }
        out += css[i];
    }
    return out;
}

}  // namespace

StyleSheet ParseStyleSheet(const std::string& css_text,
                           CascadeOrigin origin) {
    StyleSheet sheet;
    sheet.origin = origin;
    const std::string css = StripComments(css_text);
    std::size_t i = 0;
    while (i < css.size()) {
        const std::size_t open = css.find('{', i);
        if (open == std::string::npos) break;
        const std::size_t close = css.find('}', open);
        if (close == std::string::npos) break;
        const std::string selector_text = Trim(css.substr(i, open - i));
        const std::string body = css.substr(open + 1, close - open - 1);
        i = close + 1;
        if (selector_text.empty()) continue;

        std::vector<StyleDeclaration> decls;
        std::size_t d = 0;
        while (d < body.size()) {
            const std::size_t semi = body.find(';', d);
            const std::string decl = body.substr(
                d, semi == std::string::npos ? semi : semi - d);
            d = semi == std::string::npos ? body.size() : semi + 1;
            const std::size_t colon = decl.find(':');
            if (colon == std::string::npos) continue;
            auto prop = Trim(decl.substr(0, colon));
            auto val = Trim(decl.substr(colon + 1));
            if (!prop.empty() && !val.empty())
                decls.push_back({std::move(prop), std::move(val)});
        }
        if (decls.empty()) continue;

        for (auto& sel : ParseSelectorList(selector_text)) {
            StyleRule rule;
            rule.selector = std::move(sel);
            rule.declarations = decls;
            rule.text = selector_text;
            sheet.rules.push_back(std::move(rule));
        }
    }
    return sheet;
}

}  // namespace XER::UI::CSS
