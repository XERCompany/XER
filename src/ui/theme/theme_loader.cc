#include "theme_loader.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace XER::UI::Theme {
namespace {

// Minimal JSON parser sufficient for theme files: objects, arrays,
// strings, numbers, true/false/null, tolerant of trailing commas.
struct Json {
    enum class Kind { kNull, kBool, kNum, kStr, kArr, kObj };
    Kind kind = Kind::kNull;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Json> arr;
    std::unordered_map<std::string, Json> obj;
};

class Parser {
public:
    Parser(const char* p) : p_(p) {}
    bool Parse(Json* out, std::string* err) {
        SkipWs();
        if (!Value(out, err)) return false;
        SkipWs();
        if (*p_ != '\0') {
            *err = "trailing characters";
            return false;
        }
        return true;
    }

private:
    const char* p_;
    void SkipWs() {
        while (*p_ != '\0' &&
               (std::isspace((unsigned char)*p_) != 0)) {
            ++p_;
        }
    }
    bool Value(Json* out, std::string* err) {
        SkipWs();
        switch (*p_) {
            case '{': {
                ++p_;
                out->kind = Json::Kind::kObj;
                SkipWs();
                if (*p_ == '}') { ++p_; return true; }
                while (true) {
                    SkipWs();
                    if (*p_ != '"') {
                        *err = "expected object key";
                        return false;
                    }
                    std::string key;
                    if (!String(&key, err)) return false;
                    SkipWs();
                    if (*p_ != ':') {
                        *err = "expected ':'";
                        return false;
                    }
                    ++p_;
                    Json v;
                    if (!Value(&v, err)) return false;
                    out->obj[std::move(key)] = std::move(v);
                    SkipWs();
                    if (*p_ == ',') { ++p_; SkipWs(); continue; }
                    if (*p_ == '}') { ++p_; return true; }
                    *err = "expected ',' or '}'";
                    return false;
                }
            }
            case '[': {
                ++p_;
                out->kind = Json::Kind::kArr;
                SkipWs();
                if (*p_ == ']') { ++p_; return true; }
                while (true) {
                    Json v;
                    if (!Value(&v, err)) return false;
                    out->arr.push_back(std::move(v));
                    SkipWs();
                    if (*p_ == ',') { ++p_; SkipWs(); continue; }
                    if (*p_ == ']') { ++p_; return true; }
                    *err = "expected ',' or ']'";
                    return false;
                }
            }
            case '"':
                out->kind = Json::Kind::kStr;
                return String(&out->str, err);
            default: {
                if (std::strncmp(p_, "true", 4) == 0) {
                    p_ += 4;
                    out->kind = Json::Kind::kBool;
                    out->b = true;
                    return true;
                }
                if (std::strncmp(p_, "false", 5) == 0) {
                    p_ += 5;
                    out->kind = Json::Kind::kBool;
                    out->b = false;
                    return true;
                }
                if (std::strncmp(p_, "null", 4) == 0) {
                    p_ += 4;
                    return true;
                }
                char* end = nullptr;
                const double d = std::strtod(p_, &end);
                if (end == p_) {
                    *err = "expected value";
                    return false;
                }
                p_ = end;
                out->kind = Json::Kind::kNum;
                out->num = d;
                return true;
            }
        }
    }
    bool String(std::string* out, std::string* err) {
        ++p_;  // opening quote
        out->clear();
        while (*p_ != '\0' && *p_ != '"') {
            if (*p_ == '\\') {
                ++p_;
                switch (*p_) {
                    case 'n': out->push_back('\n'); break;
                    case 't': out->push_back('\t'); break;
                    case '"': out->push_back('"'); break;
                    case '\\': out->push_back('\\'); break;
                    case '/': out->push_back('/'); break;
                    case 'u': {
                        if (p_[1] == '\0' || p_[2] == '\0' ||
                            p_[3] == '\0' || p_[4] == '\0') {
                            *err = "bad \\u escape";
                            return false;
                        }
                        unsigned cp = std::strtoul(p_ + 1, nullptr, 16);
                        p_ += 4;
                        // Minimal UTF-8 encode (BMP only).
                        if (cp < 0x80) {
                            out->push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out->push_back(
                                static_cast<char>(0xC0 | (cp >> 6)));
                            out->push_back(
                                static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out->push_back(
                                static_cast<char>(0xE0 | (cp >> 12)));
                            out->push_back(static_cast<char>(
                                0x80 | ((cp >> 6) & 0x3F)));
                            out->push_back(
                                static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        *err = "bad escape";
                        return false;
                }
                ++p_;
            } else {
                out->push_back(*p_++);
            }
        }
        if (*p_ != '"') {
            *err = "unterminated string";
            return false;
        }
        ++p_;
        return true;
    }
};

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ParseColor(const std::string& s, Color* out) {
    if (s.empty()) return false;
    if (s[0] == '#') {
        const size_t n = s.size() - 1;
        unsigned r = 0, g = 0, b = 0, a = 255;
        if (n == 3) {
            const int rn = HexNibble(s[1]), gn = HexNibble(s[2]),
                      bn = HexNibble(s[3]);
            if (rn < 0 || gn < 0 || bn < 0) return false;
            r = rn * 17;
            g = gn * 17;
            b = bn * 17;
        } else if (n == 6 || n == 8) {
            const int vals[8] = {HexNibble(s[1]), HexNibble(s[2]),
                                 HexNibble(s[3]), HexNibble(s[4]),
                                 HexNibble(s[5]), HexNibble(s[6]),
                                 n == 8 ? HexNibble(s[7]) : 0,
                                 n == 8 ? HexNibble(s[8]) : 0};
            for (int i = 0; i < (n == 8 ? 8 : 6); ++i) {
                if (vals[i] < 0) return false;
            }
            r = (vals[0] << 4) | vals[1];
            g = (vals[2] << 4) | vals[3];
            b = (vals[4] << 4) | vals[5];
            if (n == 8) {
                a = (vals[6] << 4) | vals[7];
            }
        } else {
            return false;
        }
        *out = {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
        return true;
    }
    // rgb(r,g,b) / rgba(r,g,b,a)
    if (s.rfind("rgb", 0) == 0) {
        float r = 0, g = 0, b = 0, a = 1.0f;
        if (std::sscanf(s.c_str(), "rgba(%f,%f,%f,%f)", &r, &g, &b,
                        &a) == 4 ||
            std::sscanf(s.c_str(), "rgb(%f,%f,%f)", &r, &g, &b) == 3) {
            *out = {r / 255.0f, g / 255.0f, b / 255.0f,
                    a > 1.0f ? a / 255.0f : a};
            return true;
        }
    }
    return false;
}

Tokens BaseTokens(Variant v) {
    Provider p;
    p.SetVariant(v);
    return p.GetTokens();
}

void ApplyColorKey(Tokens* t, const std::string& key,
                   const Color& c) {
    static const std::unordered_map<std::string,
                                    Color Tokens::*>
        kColorKeys = {
            {"background", &Tokens::background},
            {"foreground", &Tokens::foreground},
            {"surface", &Tokens::surface},
            {"muted", &Tokens::muted},
            {"border", &Tokens::border},
            {"accent", &Tokens::accent},
            {"accent_foreground", &Tokens::accent_foreground},
            {"focus", &Tokens::focus},
            {"secondary", &Tokens::secondary},
            {"hover", &Tokens::hover},
            {"ring", &Tokens::ring},
            {"danger", &Tokens::danger},
            {"danger_foreground", &Tokens::danger_foreground},
            {"panel", &Tokens::panel},
            {"input", &Tokens::input},
            {"pressed", &Tokens::pressed},
            {"disabled", &Tokens::disabled},
            {"selection", &Tokens::selection},
            {"status_ok", &Tokens::status_ok},
            {"status_warn", &Tokens::status_warn},
            {"status_bad", &Tokens::status_bad},
        };
    const auto it = kColorKeys.find(key);
    if (it != kColorKeys.end()) {
        t->*(it->second) = c;
    }
}

void ApplyMetricKey(Tokens* t, const std::string& key, float v) {
    static const std::unordered_map<std::string,
                                    float Tokens::*>
        kMetricKeys = {
            {"radius_small", &Tokens::radius_small},
            {"radius_medium", &Tokens::radius_medium},
            {"radius_large", &Tokens::radius_large},
            {"spacing_small", &Tokens::spacing_small},
            {"spacing_medium", &Tokens::spacing_medium},
            {"spacing_large", &Tokens::spacing_large},
            {"font_small", &Tokens::font_small},
            {"font_body", &Tokens::font_body},
            {"font_title", &Tokens::font_title},
        };
    const auto it = kMetricKeys.find(key);
    if (it != kMetricKeys.end()) {
        t->*(it->second) = v;
    }
}

}  // namespace

bool ThemeLoader::Parse(const std::string& json, Tokens* tokens_out,
                        Variant* variant_out,
                        std::string* error_out) {
    Json root;
    std::string err;
    if (!Parser(json.c_str()).Parse(&root, &err)) {
        if (error_out) *error_out = "theme json: " + err;
        return false;
    }
    if (root.kind != Json::Kind::kObj) {
        if (error_out) *error_out = "theme json: root must be object";
        return false;
    }
    Variant variant = Variant::Dark;
    const auto vit = root.obj.find("variant");
    if (vit != root.obj.end() &&
        vit->second.kind == Json::Kind::kStr) {
        if (vit->second.str == "light") {
            variant = Variant::Light;
        }
    }
    Tokens tokens = BaseTokens(variant);
    const auto cit = root.obj.find("colors");
    if (cit != root.obj.end() &&
        cit->second.kind == Json::Kind::kObj) {
        for (const auto& [key, v] : cit->second.obj) {
            if (v.kind != Json::Kind::kStr) continue;
            Color c{};
            if (!ParseColor(v.str, &c)) {
                if (error_out) {
                    *error_out = "theme json: bad color '" + v.str +
                                 "' for '" + key + "'";
                }
                return false;
            }
            ApplyColorKey(&tokens, key, c);
        }
    }
    const auto mit = root.obj.find("metrics");
    if (mit != root.obj.end() &&
        mit->second.kind == Json::Kind::kObj) {
        for (const auto& [key, v] : mit->second.obj) {
            if (v.kind == Json::Kind::kNum) {
                ApplyMetricKey(&tokens, key,
                               static_cast<float>(v.num));
            }
        }
    }
    if (tokens_out) *tokens_out = tokens;
    if (variant_out) *variant_out = variant;
    return true;
}

bool ThemeLoader::LoadFile(const std::string& path, Tokens* tokens_out,
                           Variant* variant_out,
                           std::string* error_out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error_out) *error_out = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return Parse(ss.str(), tokens_out, variant_out, error_out);
}

bool ThemeLoader::ApplyToProvider(Provider& provider,
                                  const std::string& path,
                                  std::string* error_out) {
    Tokens tokens{};
    Variant variant = Variant::Dark;
    if (!LoadFile(path, &tokens, &variant, error_out)) {
        return false;
    }
    provider.SetTokens(tokens, variant);
    return true;
}

}  // namespace XER::UI::Theme
