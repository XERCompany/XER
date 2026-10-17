#include "node_graph/node_graph_json.h"
 
#include <cmath>
#include <cstdio>
#include <cstdlib>
 
namespace XER::NodeGraph {
 
namespace {
 
const std::string& EmptyString() {
    static const std::string kEmpty;
    return kEmpty;
}
 
const Json& NullJson() {
    static const Json kNull;
    return kNull;
}
 
void AppendEscaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}
 
void AppendNumber(std::string& out, double v) {
    if (!std::isfinite(v)) {
        out += "null";
        return;
    }
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        out += std::to_string(static_cast<long long>(v));
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    out += buf;
}
 
void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}
 
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}
 
    bool Run(Json* out, std::string* error) {
        SkipWs();
        if (!ParseValue(out, 0)) {
            if (error) *error = error_ + " at offset " + std::to_string(pos_);
            return false;
        }
        SkipWs();
        if (pos_ != text_.size()) {
            if (error) *error = "trailing characters at offset " + std::to_string(pos_);
            return false;
        }
        return true;
    }
 
private:
    bool Fail(const char* msg) {
        error_ = msg;
        return false;
    }
 
    void SkipWs() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) {
            ++pos_;
        }
    }
 
    bool Consume(std::string_view lit) {
        if (text_.substr(pos_, lit.size()) == lit) {
            pos_ += lit.size();
            return true;
        }
        return false;
    }
 
    bool ParseValue(Json* out, int depth) {
        if (depth > 256) return Fail("nesting too deep");
        if (pos_ >= text_.size()) return Fail("unexpected end of input");
        const char c = text_[pos_];
        if (c == '{') return ParseObject(out, depth);
        if (c == '[') return ParseArray(out, depth);
        if (c == '"') {
            std::string s;
            if (!ParseString(&s)) return false;
            *out = Json(std::move(s));
            return true;
        }
        if (Consume("true")) { *out = Json(true); return true; }
        if (Consume("false")) { *out = Json(false); return true; }
        if (Consume("null")) { *out = Json(); return true; }
        return ParseNumber(out);
    }
 
    bool ParseNumber(Json* out) {
        const size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                ++pos_;
            } else {
                break;
            }
        }
        if (start == pos_) return Fail("invalid value");
        const std::string num(text_.substr(start, pos_ - start));
        char* end = nullptr;
        const double v = std::strtod(num.c_str(), &end);
        if (end == num.c_str()) return Fail("invalid number");
        *out = Json(v);
        return true;
    }
 
    bool ParseHex4(uint32_t* cp) {
        if (pos_ + 4 > text_.size()) return Fail("bad unicode escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return Fail("bad unicode escape");
        }
        *cp = v;
        return true;
    }
 
    bool ParseString(std::string* out) {
        ++pos_;  // opening quote
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) break;
            const char e = text_[pos_++];
            switch (e) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!ParseHex4(&cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && Consume("\\u")) {
                        uint32_t lo = 0;
                        if (!ParseHex4(&lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(*out, cp);
                    break;
                }
                default: return Fail("bad escape");
            }
        }
        return Fail("unterminated string");
    }
 
    bool ParseArray(Json* out, int depth) {
        ++pos_;
        Json::Array arr;
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            *out = Json(std::move(arr));
            return true;
        }
        while (true) {
            SkipWs();
            Json item;
            if (!ParseValue(&item, depth + 1)) return false;
            arr.push_back(std::move(item));
            SkipWs();
            if (pos_ >= text_.size()) return Fail("unterminated array");
            if (text_[pos_] == ',') { ++pos_; continue; }
            if (text_[pos_] == ']') { ++pos_; break; }
            return Fail("expected ',' or ']'");
        }
        *out = Json(std::move(arr));
        return true;
    }
 
    bool ParseObject(Json* out, int depth) {
        ++pos_;
        Json::Object obj;
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            *out = Json(std::move(obj));
            return true;
        }
        while (true) {
            SkipWs();
            if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("expected object key");
            std::string key;
            if (!ParseString(&key)) return false;
            SkipWs();
            if (pos_ >= text_.size() || text_[pos_] != ':') return Fail("expected ':'");
            ++pos_;
            SkipWs();
            Json value;
            if (!ParseValue(&value, depth + 1)) return false;
            obj[std::move(key)] = std::move(value);
            SkipWs();
            if (pos_ >= text_.size()) return Fail("unterminated object");
            if (text_[pos_] == ',') { ++pos_; continue; }
            if (text_[pos_] == '}') { ++pos_; break; }
            return Fail("expected ',' or '}'");
        }
        *out = Json(std::move(obj));
        return true;
    }
 
    std::string_view text_;
    size_t pos_ = 0;
    std::string error_;
};
 
}  // namespace
 
bool Json::AsBool(bool fallback) const {
    if (const auto* b = std::get_if<bool>(&data_)) return *b;
    if (const auto* d = std::get_if<double>(&data_)) return *d != 0.0;
    return fallback;
}
 
double Json::AsNumber(double fallback) const {
    if (const auto* d = std::get_if<double>(&data_)) return *d;
    if (const auto* b = std::get_if<bool>(&data_)) return *b ? 1.0 : 0.0;
    return fallback;
}
 
int64_t Json::AsInt(int64_t fallback) const {
    if (const auto* d = std::get_if<double>(&data_)) return static_cast<int64_t>(*d);
    return fallback;
}
 
const std::string& Json::AsString() const {
    if (const auto* s = std::get_if<std::string>(&data_)) return *s;
    return EmptyString();
}
 
const Json::Array& Json::AsArray() const {
    static const Array kEmpty;
    if (const auto* a = std::get_if<Array>(&data_)) return *a;
    return kEmpty;
}
 
const Json::Object& Json::AsObject() const {
    static const Object kEmpty;
    if (const auto* o = std::get_if<Object>(&data_)) return *o;
    return kEmpty;
}
 
Json::Array& Json::MutableArray() {
    if (!IsArray()) data_ = Array{};
    return std::get<Array>(data_);
}
 
Json::Object& Json::MutableObject() {
    if (!IsObject()) data_ = Object{};
    return std::get<Object>(data_);
}
 
bool Json::Has(const std::string& key) const {
    const auto* o = std::get_if<Object>(&data_);
    return o != nullptr && o->count(key) != 0;
}
 
const Json& Json::operator[](const std::string& key) const {
    if (const auto* o = std::get_if<Object>(&data_)) {
        auto it = o->find(key);
        if (it != o->end()) return it->second;
    }
    return NullJson();
}
 
Json& Json::operator[](const std::string& key) { return MutableObject()[key]; }
 
void Json::Push(Json value) { MutableArray().push_back(std::move(value)); }
 
std::string Json::Dump(int indent) const {
    std::string out;
    DumpTo(out, indent, 0);
    return out;
}
 
void Json::DumpTo(std::string& out, int indent, int depth) const {
    auto newline = [&](int d) {
        if (indent < 0) return;
        out.push_back('\n');
        out.append(static_cast<size_t>(indent * d), ' ');
    };
    if (IsNull()) { out += "null"; return; }
    if (const auto* b = std::get_if<bool>(&data_)) { out += *b ? "true" : "false"; return; }
    if (const auto* d = std::get_if<double>(&data_)) { AppendNumber(out, *d); return; }
    if (const auto* s = std::get_if<std::string>(&data_)) { AppendEscaped(out, *s); return; }
    if (const auto* a = std::get_if<Array>(&data_)) {
        if (a->empty()) { out += "[]"; return; }
        out.push_back('[');
        for (size_t i = 0; i < a->size(); ++i) {
            if (i) out.push_back(',');
            newline(depth + 1);
            (*a)[i].DumpTo(out, indent, depth + 1);
        }
        newline(depth);
        out.push_back(']');
        return;
    }
    const auto& o = std::get<Object>(data_);
    if (o.empty()) { out += "{}"; return; }
    out.push_back('{');
    bool first = true;
    for (const auto& [k, v] : o) {
        if (!first) out.push_back(',');
        first = false;
        newline(depth + 1);
        AppendEscaped(out, k);
        out.push_back(':');
        if (indent >= 0) out.push_back(' ');
        v.DumpTo(out, indent, depth + 1);
    }
    newline(depth);
    out.push_back('}');
}
 
bool Json::Parse(std::string_view text, Json* out, std::string* error_out) {
    Parser parser(text);
    Json result;
    if (!parser.Run(&result, error_out)) return false;
    if (out) *out = std::move(result);
    return true;
}
 
}  // namespace XER::NodeGraph