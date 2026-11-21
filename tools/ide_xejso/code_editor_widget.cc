// code_editor_widget.cc — see code_editor_widget.h.
//
// Tokenizer notes: single-pass per line, byte-oriented (UTF-8 safe —
// identifiers and strings only ever split on ASCII boundaries, so
// multi-byte sequences ride inside whatever span contains them).
// Cross-line state covers block comments and multi-line strings only;
// everything else is line-local.
#include "code_editor_widget.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>

#include "content/clipboard/clipboard.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::IdeXejso {
namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::KeyCode;
using XER::UI::Widgets::MouseButton;

// ---- language tables --------------------------------------------------------

const std::unordered_set<std::string_view>& CppKeywords() {
    static const std::unordered_set<std::string_view> kw = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand",
        "bitor", "break", "case", "catch", "class", "compl", "concept",
        "const", "consteval", "constexpr", "constinit", "const_cast",
        "continue", "co_await", "co_return", "co_yield", "decltype",
        "default", "delete", "do", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "for", "friend", "goto",
        "if", "inline", "mutable", "namespace", "new", "noexcept", "not",
        "not_eq", "nullptr", "operator", "or", "or_eq", "private",
        "protected", "public", "register", "reinterpret_cast", "requires",
        "return", "sizeof", "static", "static_assert", "static_cast",
        "struct", "switch", "template", "this", "thread_local", "throw",
        "true", "try", "typedef", "typeid", "typename", "union", "using",
        "virtual", "volatile", "while", "xor", "xor_eq",
    };
    return kw;
}

const std::unordered_set<std::string_view>& CppTypes() {
    static const std::unordered_set<std::string_view> types = {
        "bool", "char", "char8_t", "char16_t", "char32_t", "double",
        "float", "int", "long", "short", "signed", "unsigned", "void",
        "wchar_t", "size_t", "ssize_t", "ptrdiff_t", "intptr_t",
        "uintptr_t", "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t", "std", "string",
        "string_view", "vector", "map", "set", "unordered_map",
        "unordered_set", "pair", "optional", "variant", "function",
        "shared_ptr", "unique_ptr", "weak_ptr", "span", "array",
    };
    return types;
}

const std::unordered_set<std::string_view>& JsKeywords() {
    static const std::unordered_set<std::string_view> kw = {
        "async", "await", "break", "case", "catch", "class", "const",
        "continue", "debugger", "default", "delete", "do", "else",
        "export", "extends", "false", "finally", "for", "from",
        "function", "get", "if", "import", "in", "instanceof", "let",
        "new", "null", "of", "return", "set", "static", "super",
        "switch", "this", "throw", "true", "try", "typeof", "undefined",
        "var", "void", "while", "with", "yield",
    };
    return kw;
}

const std::unordered_set<std::string_view>& JsBuiltins() {
    static const std::unordered_set<std::string_view> types = {
        "Array", "ArrayBuffer", "BigInt", "Boolean", "console",
        "DataView", "Date", "Error", "Float32Array", "Float64Array",
        "Function", "Infinity", "Int8Array", "Int16Array", "Int32Array",
        "JSON", "Map", "Math", "NaN", "Number", "Object", "Promise",
        "Proxy", "Reflect", "RegExp", "Set", "String", "Symbol",
        "TypeError", "Uint8Array", "Uint8ClampedArray", "Uint16Array",
        "Uint32Array", "WeakMap", "WeakSet", "xer_ext", "NativeImport",
        "ext",
    };
    return types;
}

const std::unordered_set<std::string_view>& JsonKeywords() {
    static const std::unordered_set<std::string_view> kw = {
        "true", "false", "null",
    };
    return kw;
}

// ---- char classes -------------------------------------------------------------

bool IsIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_' || c >= 0x80;
}
bool IsIdent(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '$' || c >= 0x80;
}
bool IsDigit(unsigned char c) { return std::isdigit(c); }

bool IsContinuation(unsigned char b) { return (b & 0xC0u) == 0x80u; }

std::size_t PrevCodePoint(std::string_view t, std::size_t p) {
    if (p == 0) return 0;
    --p;
    while (p > 0 && IsContinuation(static_cast<unsigned char>(t[p]))) --p;
    return p;
}
std::size_t NextCodePoint(std::string_view t, std::size_t p) {
    if (p >= t.size()) return t.size();
    ++p;
    while (p < t.size() &&
           IsContinuation(static_cast<unsigned char>(t[p]))) {
        ++p;
    }
    return p;
}
std::size_t Utf8SeqLen(unsigned char lead) {
    if (lead < 0x80u) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;
}

// ---- shared scanners ----------------------------------------------------------

// Emits [cursor, i) with `kind` when non-empty, merges into the
// previous span when the kinds match.
void Emit(std::vector<HighlightSpan>& out, std::size_t begin,
          std::size_t end, TokKind kind) {
    if (end <= begin) return;
    if (!out.empty() && out.back().kind == kind &&
        out.back().end == begin) {
        out.back().end = end;
        return;
    }
    out.push_back({begin, end, kind});
}

// Scans a single-line quoted string starting at `i` (the quote char
// `q`). Returns the index just past the closing quote, or line.size()
// when unterminated.
std::size_t ScanQuoted(std::string_view line, std::size_t i, char q) {
    ++i;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '\\') {
            i += 2;
            continue;
        }
        if (c == q) return i + 1;
        ++i;
    }
    return line.size();
}

// Scans a C++ raw string R"delim( ... )delim". `start` indexes the R.
// Returns end index; sets `open=true` + `delim` when the string runs
// past end-of-line.
std::size_t ScanRawString(std::string_view line, std::size_t start,
                          bool* open, std::string* delim) {
    // start points at 'R'; find '(' after "delim.
    std::size_t i = start + 2;  // past R"
    const std::size_t delim_begin = i;
    while (i < line.size() && line[i] != '(') ++i;
    if (i >= line.size()) {
        // R" without ( — malformed; treat R as ident, " as string.
        return start + 1;
    }
    std::string d(line.substr(delim_begin, i - delim_begin));
    const std::string closer = ")" + d + "\"";
    const auto close = line.find(closer, i + 1);
    if (close == std::string_view::npos) {
        *open = true;
        *delim = std::move(d);
        return line.size();
    }
    return close + closer.size();
}

// Continues an open raw string at line start.
std::size_t ResumeRawString(std::string_view line,
                            const std::string& delim, bool* open) {
    const std::string closer = ")" + delim + "\"";
    const auto close = line.find(closer, 0);
    if (close == std::string_view::npos) {
        *open = true;
        return line.size();
    }
    *open = false;
    return close + closer.size();
}

std::size_t ScanNumber(std::string_view line, std::size_t i) {
    // Permissive: digits, letters (hex/suffixes), '.', '_', exponent signs.
    while (i < line.size()) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if (std::isalnum(c) || c == '.' || c == '_' || c == '\'') {
            ++i;
        } else if ((c == '+' || c == '-') && i > 0 &&
                   (line[i - 1] == 'e' || line[i - 1] == 'E' ||
                    line[i - 1] == 'p' || line[i - 1] == 'P')) {
            ++i;
        } else {
            break;
        }
    }
    return i;
}

// ---- per-language line tokenizers --------------------------------------------

void TokenizeCppLine(std::string_view line,
                     std::vector<HighlightSpan>& out,
                     LineLexState& st) {
    std::size_t i = 0;
    if (st.mode == 1) {  // inside /* */
        const auto close = line.find("*/");
        if (close == std::string_view::npos) {
            Emit(out, 0, line.size(), TokKind::Comment);
            return;
        }
        Emit(out, 0, close + 2, TokKind::Comment);
        i = close + 2;
        st.mode = 0;
    } else if (st.mode == 2) {  // inside R"delim(
        bool open = false;
        const std::size_t end =
            ResumeRawString(line, st.aux, &open);
        Emit(out, 0, end, TokKind::String);
        if (open) return;
        i = end;
        st.mode = 0;
        st.aux.clear();
    }

    bool first_token = true;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t') {
            const std::size_t b = i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
            Emit(out, b, i, TokKind::Plain);
            continue;
        }
        if (first_token && c == '#') {
            Emit(out, i, line.size(), TokKind::Preproc);
            return;
        }
        first_token = false;
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            Emit(out, i, line.size(), TokKind::Comment);
            return;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            const auto close = line.find("*/", i + 2);
            if (close == std::string_view::npos) {
                Emit(out, i, line.size(), TokKind::Comment);
                st.mode = 1;
                return;
            }
            Emit(out, i, close + 2, TokKind::Comment);
            i = close + 2;
            continue;
        }
        if (c == 'R' && i + 1 < line.size() && line[i + 1] == '"' &&
            (i == 0 ||
             !IsIdent(static_cast<unsigned char>(line[i - 1])))) {
            bool open = false;
            std::string d;
            const std::size_t end =
                ScanRawString(line, i, &open, &d);
            Emit(out, i, end, TokKind::String);
            if (open) {
                st.mode = 2;
                st.aux = std::move(d);
                return;
            }
            i = end;
            continue;
        }
        if (c == '"' || c == '\'') {
            const std::size_t end = ScanQuoted(line, i, c);
            Emit(out, i, end, TokKind::String);
            i = end;
            continue;
        }
        if (IsDigit(static_cast<unsigned char>(c))) {
            const std::size_t end = ScanNumber(line, i);
            Emit(out, i, end, TokKind::Number);
            i = end;
            continue;
        }
        if (IsIdentStart(static_cast<unsigned char>(c))) {
            const std::size_t b = i;
            while (i < line.size() &&
                   IsIdent(static_cast<unsigned char>(line[i]))) {
                i += Utf8SeqLen(static_cast<unsigned char>(line[i]));
            }
            const std::string_view word = line.substr(b, i - b);
            if (CppKeywords().count(word)) {
                Emit(out, b, i, TokKind::Keyword);
            } else if (CppTypes().count(word)) {
                Emit(out, b, i, TokKind::Type);
            } else {
                Emit(out, b, i, TokKind::Plain);
            }
            continue;
        }
        Emit(out, i, i + 1, TokKind::Punct);
        ++i;
    }
}

void TokenizeJsLine(std::string_view line,
                    std::vector<HighlightSpan>& out,
                    LineLexState& st) {
    std::size_t i = 0;
    if (st.mode == 1) {
        const auto close = line.find("*/");
        if (close == std::string_view::npos) {
            Emit(out, 0, line.size(), TokKind::Comment);
            return;
        }
        Emit(out, 0, close + 2, TokKind::Comment);
        i = close + 2;
        st.mode = 0;
    } else if (st.mode == 2) {  // inside `...` template literal
        bool closed = false;
        while (i < line.size()) {
            if (line[i] == '\\') { i += 2; continue; }
            if (line[i] == '`') { closed = true; ++i; break; }
            ++i;
        }
        Emit(out, 0, i, TokKind::String);
        if (!closed) return;
        st.mode = 0;
    }

    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t') {
            const std::size_t b = i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
            Emit(out, b, i, TokKind::Plain);
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            Emit(out, i, line.size(), TokKind::Comment);
            return;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            const auto close = line.find("*/", i + 2);
            if (close == std::string_view::npos) {
                Emit(out, i, line.size(), TokKind::Comment);
                st.mode = 1;
                return;
            }
            Emit(out, i, close + 2, TokKind::Comment);
            i = close + 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            const std::size_t end = ScanQuoted(line, i, c);
            Emit(out, i, end, TokKind::String);
            i = end;
            continue;
        }
        if (c == '`') {
            std::size_t j = i + 1;
            bool closed = false;
            while (j < line.size()) {
                if (line[j] == '\\') { j += 2; continue; }
                if (line[j] == '`') { closed = true; ++j; break; }
                ++j;
            }
            Emit(out, i, j, TokKind::String);
            if (!closed) {
                st.mode = 2;
                return;
            }
            i = j;
            continue;
        }
        if (IsDigit(static_cast<unsigned char>(c))) {
            const std::size_t end = ScanNumber(line, i);
            Emit(out, i, end, TokKind::Number);
            i = end;
            continue;
        }
        if (IsIdentStart(static_cast<unsigned char>(c)) || c == '$') {
            const std::size_t b = i;
            while (i < line.size() &&
                   IsIdent(static_cast<unsigned char>(line[i]))) {
                i += Utf8SeqLen(static_cast<unsigned char>(line[i]));
            }
            const std::string_view word = line.substr(b, i - b);
            if (JsKeywords().count(word)) {
                Emit(out, b, i, TokKind::Keyword);
            } else if (JsBuiltins().count(word)) {
                Emit(out, b, i, TokKind::Type);
            } else {
                Emit(out, b, i, TokKind::Plain);
            }
            continue;
        }
        Emit(out, i, i + 1, TokKind::Punct);
        ++i;
    }
}

void TokenizeTomlLine(std::string_view line,
                      std::vector<HighlightSpan>& out,
                      LineLexState& st) {
    (void)st;  // v1: no multi-line constructs (basic strings only)
    std::size_t i = 0;
    bool first_token = true;
    bool after_eq = false;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t') {
            const std::size_t b = i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
            Emit(out, b, i, TokKind::Plain);
            continue;
        }
        if (c == '#') {
            Emit(out, i, line.size(), TokKind::Comment);
            return;
        }
        if (first_token && c == '[') {
            // Section header [a.b] — color to the closing bracket.
            const auto close = line.find(']', i + 1);
            const std::size_t end =
                close == std::string_view::npos ? line.size() : close + 1;
            Emit(out, i, end, TokKind::Preproc);
            i = end;
            first_token = false;
            continue;
        }
        first_token = false;
        if (c == '"') {
            const std::size_t end = ScanQuoted(line, i, '"');
            Emit(out, i, end, TokKind::String);
            i = end;
            continue;
        }
        if (c == '\'') {  // TOML literal string: no escapes
            const auto q = line.find('\'', i + 1);
            const std::size_t end =
                q == std::string_view::npos ? line.size() : q + 1;
            Emit(out, i, end, TokKind::String);
            i = end;
            continue;
        }
        if (c == '=') {
            Emit(out, i, i + 1, TokKind::Punct);
            ++i;
            after_eq = true;
            continue;
        }
        if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',' ||
            c == '.') {
            Emit(out, i, i + 1, TokKind::Punct);
            ++i;
            continue;
        }
        if (IsDigit(static_cast<unsigned char>(c)) ||
            (after_eq && (c == '-' || c == '+'))) {
            const std::size_t end = ScanNumber(line, i);
            Emit(out, i, end, TokKind::Number);
            i = end;
            continue;
        }
        if (IsIdentStart(static_cast<unsigned char>(c)) || c == '-') {
            const std::size_t b = i;
            while (i < line.size() &&
                   IsIdent(static_cast<unsigned char>(line[i]))) {
                ++i;
            }
            const std::string_view word = line.substr(b, i - b);
            if (word == "true" || word == "false") {
                Emit(out, b, i, TokKind::Keyword);
            } else {
                Emit(out, b, i,
                     after_eq ? TokKind::Plain : TokKind::Type);
            }
            continue;
        }
        Emit(out, i, i + 1, TokKind::Punct);
        ++i;
    }
}

void TokenizeJsonLine(std::string_view line,
                      std::vector<HighlightSpan>& out,
                      LineLexState& st) {
    (void)st;
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t') {
            const std::size_t b = i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
            Emit(out, b, i, TokKind::Plain);
            continue;
        }
        if (c == '"') {
            const std::size_t end = ScanQuoted(line, i, '"');
            // Key strings (followed by ':') read as Type, values as String.
            std::size_t k = end;
            while (k < line.size() &&
                   (line[k] == ' ' || line[k] == '\t')) ++k;
            Emit(out, i, end,
                 k < line.size() && line[k] == ':' ? TokKind::Type
                                                 : TokKind::String);
            i = end;
            continue;
        }
        if (IsDigit(static_cast<unsigned char>(c)) || c == '-') {
            const std::size_t end = ScanNumber(line, i);
            Emit(out, i, end, TokKind::Number);
            i = end;
            continue;
        }
        if (IsIdentStart(static_cast<unsigned char>(c))) {
            const std::size_t b = i;
            while (i < line.size() &&
                   IsIdent(static_cast<unsigned char>(line[i]))) ++i;
            Emit(out, b, i,
                 JsonKeywords().count(line.substr(b, i - b))
                     ? TokKind::Keyword
                     : TokKind::Plain);
            continue;
        }
        Emit(out, i, i + 1, TokKind::Punct);
        ++i;
    }
}

// ---- CSS ---------------------------------------------------------------------
// st.mode bit flags: 1 = inside a /* */ comment, 2 = inside a { } block.
// (In an HTML <style> block these live at LineLexState modes 6..9.)

void TokenizeCssLine(std::string_view line,
                     std::vector<HighlightSpan>& out,
                     LineLexState& st) {
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (st.mode & 1) {  // inside /* */
            const auto close = line.find("*/", i);
            if (close == std::string_view::npos) {
                Emit(out, i, line.size(), TokKind::Comment);
                return;
            }
            Emit(out, i, close + 2, TokKind::Comment);
            i = close + 2;
            st.mode &= ~1;
            continue;
        }
        if (c == ' ' || c == '\t') {
            const std::size_t b = i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
            Emit(out, b, i, TokKind::Plain);
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            const auto close = line.find("*/", i + 2);
            if (close == std::string_view::npos) {
                Emit(out, i, line.size(), TokKind::Comment);
                st.mode |= 1;
                return;
            }
            Emit(out, i, close + 2, TokKind::Comment);
            i = close + 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            const std::size_t end = ScanQuoted(line, i, c);
            Emit(out, i, end, TokKind::String);
            i = end;
            continue;
        }
        if (!(st.mode & 2)) {
            // Selector context: everything up to '{' is the selector.
            if (c == '{') {
                Emit(out, i, i + 1, TokKind::Punct);
                ++i;
                st.mode |= 2;
                continue;
            }
            if (c == '}') {
                Emit(out, i, i + 1, TokKind::Punct);
                ++i;
                continue;
            }
            if (c == '@') {  // @media/@keyframes/...
                const std::size_t b = i++;
                while (i < line.size() &&
                       IsIdent(static_cast<unsigned char>(line[i]))) ++i;
                Emit(out, b, i, TokKind::Preproc);
                continue;
            }
            const std::size_t b = i;
            while (i < line.size() && line[i] != '{' && line[i] != '/' &&
                   line[i] != '"' && line[i] != '\'' &&
                   line[i] != ' ' && line[i] != '\t') ++i;
            if (b == i) {
                Emit(out, i, i + 1, TokKind::Punct);
                ++i;
                continue;
            }
            Emit(out, b, i, TokKind::Keyword);
            continue;
        }
        // Inside a declaration block.
        if (c == '}') {
            Emit(out, i, i + 1, TokKind::Punct);
            ++i;
            st.mode &= ~2;
            continue;
        }
        if (c == '{') {
            Emit(out, i, i + 1, TokKind::Punct);
            ++i;
            continue;
        }
        if (c == ':' || c == ';' || c == ',' || c == '(' || c == ')' ||
            c == '%') {
            Emit(out, i, i + 1, TokKind::Punct);
            ++i;
            continue;
        }
        if (c == '!') {  // !important
            const std::size_t b = i++;
            while (i < line.size() &&
                   IsIdent(static_cast<unsigned char>(line[i]))) ++i;
            Emit(out, b, i, TokKind::Keyword);
            continue;
        }
        if (c == '#') {  // hex color
            const std::size_t b = i++;
            while (i < line.size() &&
                   std::isxdigit(static_cast<unsigned char>(line[i]))) ++i;
            Emit(out, b, i, i > b + 1 ? TokKind::Number : TokKind::Punct);
            continue;
        }
        if (IsDigit(static_cast<unsigned char>(c)) ||
            ((c == '-' || c == '+' || c == '.') && i + 1 < line.size() &&
             IsDigit(static_cast<unsigned char>(line[i + 1])))) {
            const std::size_t end = ScanNumber(line, i);
            Emit(out, i, end, TokKind::Number);
            i = end;
            continue;
        }
        // ident run — a property name when a ':' follows, else a value.
        {
            const std::size_t b = i;
            while (i < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[i])) ||
                    line[i] == '-' || line[i] == '_' ||
                    static_cast<unsigned char>(line[i]) >= 0x80)) {
                i += Utf8SeqLen(static_cast<unsigned char>(line[i]));
            }
            if (b == i) {
                Emit(out, i, i + 1, TokKind::Punct);
                ++i;
                continue;
            }
            std::size_t k = i;
            while (k < line.size() &&
                   (line[k] == ' ' || line[k] == '\t')) ++k;
            Emit(out, b, i,
                 k < line.size() && line[k] == ':' ? TokKind::Type
                                                  : TokKind::Plain);
            continue;
        }
    }
}

// ---- HTML --------------------------------------------------------------------
// Html LineLexState modes:
//    0      markup / text
//    1      inside <!-- -->
//    2      inside <!doctype ...> or <?pi ...>  (runs to '>')
//    3..5   inside <script>: JS sub-state = mode - 3
//    6..9   inside <style>:  CSS sub-state = mode - 6
//    10..12 inside a tag:    10 normal, 11 <script> open pending,
//                            12 <style> open pending
//    13..15 inside a quoted attr value, returning to 10..12
//           (aux holds the quote char)

bool IsTagNameChar(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == ':';
}

// Case-insensitive search for "</tag" at or after `from`; `tag` is
// already lowercase and must be followed by a non-name char.
std::size_t FindCloseTag(std::string_view line, std::string_view tag,
                         std::size_t from) {
    for (std::size_t p = line.find('<', from);
         p != std::string_view::npos;
         p = line.find('<', p + 1)) {
        if (p + 2 + tag.size() > line.size()) break;
        if (line[p + 1] != '/') continue;
        bool eq = true;
        for (std::size_t k = 0; k < tag.size(); ++k) {
            if (std::tolower(static_cast<unsigned char>(
                    line[p + 2 + k])) != tag[k]) {
                eq = false;
                break;
            }
        }
        if (!eq) continue;
        const std::size_t after = p + 2 + tag.size();
        if (after < line.size() &&
            IsTagNameChar(static_cast<unsigned char>(line[after]))) {
            continue;   // "</scriptx" is not "</script"
        }
        return p;
    }
    return std::string_view::npos;
}

// True when `s` ends with `q` not preceded by an odd run of backslashes
// — used to tell a closed quote from an unterminated line ending in an
// escaped one.
bool EndsWithUnescaped(std::string_view s, char q) {
    if (s.empty() || s.back() != q) return false;
    std::size_t p = s.size() - 1;
    std::size_t bs = 0;
    while (p > 0 && s[p - 1] == '\\') {
        ++bs;
        --p;
    }
    return (bs & 1) == 0;
}

void TokenizeHtmlLine(std::string_view line,
                      std::vector<HighlightSpan>& out,
                      LineLexState& st) {
    std::size_t i = 0;
    while (i < line.size()) {
        // Embedded <script>/<style>: delegate to the sub-tokenizer up
        // to the matching close tag (per-line state is enough — a line
        // starting mid-block stays in the block until "</tag").
        if (st.mode >= 3 && st.mode <= 9) {
            const bool script = st.mode <= 5;
            const std::size_t close =
                FindCloseTag(line, script ? "script" : "style", i);
            const std::size_t end = close == std::string_view::npos
                                        ? line.size()
                                        : close;
            std::vector<HighlightSpan> sub;
            LineLexState inner{script ? st.mode - 3 : st.mode - 6,
                               st.aux};
            if (script) {
                TokenizeJsLine(line.substr(i, end - i), sub, inner);
            } else {
                TokenizeCssLine(line.substr(i, end - i), sub, inner);
            }
            for (auto& sp : sub) {
                out.push_back(
                    {sp.begin + i, sp.end + i, sp.kind});
            }
            if (close == std::string_view::npos) {
                st.mode = (script ? 3 : 6) + inner.mode;
                st.aux = inner.aux;
                return;
            }
            i = close;
            st.mode = 0;
            st.aux.clear();
            continue;
        }
        if (st.mode == 1) {  // inside <!-- -->
            const auto close = line.find("-->", i);
            if (close == std::string_view::npos) {
                Emit(out, i, line.size(), TokKind::Comment);
                return;
            }
            Emit(out, i, close + 3, TokKind::Comment);
            i = close + 3;
            st.mode = 0;
            continue;
        }
        if (st.mode == 2) {  // inside <!.. / <?..
            const auto close = line.find('>', i);
            if (close == std::string_view::npos) {
                Emit(out, i, line.size(), TokKind::Preproc);
                return;
            }
            Emit(out, i, close + 1, TokKind::Preproc);
            i = close + 1;
            st.mode = 0;
            continue;
        }
        if (st.mode >= 13) {  // inside a quoted attr value
            const char q = st.aux.empty() ? '"' : st.aux.front();
            const auto close = line.find(q, i);
            if (close == std::string_view::npos) {
                Emit(out, i, line.size(), TokKind::String);
                return;
            }
            Emit(out, i, close + 1, TokKind::String);
            i = close + 1;
            st.aux.clear();
            st.mode -= 3;   // back into the tag (10..12)
            continue;
        }
        if (st.mode >= 10) {  // inside a tag
            const char c = line[i];
            if (c == ' ' || c == '\t') {
                const std::size_t b = i;
                while (i < line.size() &&
                       (line[i] == ' ' || line[i] == '\t')) ++i;
                Emit(out, b, i, TokKind::Plain);
                continue;
            }
            if (c == '"' || c == '\'') {
                const std::size_t end = ScanQuoted(line, i, c);
                Emit(out, i, end, TokKind::String);
                if (end >= line.size() &&
                    !EndsWithUnescaped(line, c)) {
                    st.aux.assign(1, c);
                    st.mode += 3;   // 13..15, same pending tag
                    return;
                }
                i = end;
                continue;
            }
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '>') {
                Emit(out, i, i + 2, TokKind::Punct);
                i += 2;
                st.mode = 0;
                continue;
            }
            if (c == '>') {
                Emit(out, i, i + 1, TokKind::Punct);
                ++i;
                st.mode = st.mode == 11  ? 3
                          : st.mode == 12 ? 6
                                          : 0;
                continue;
            }
            if (c == '<') {  // stray — let markup handle it
                st.mode = 0;
                continue;
            }
            // attr name / unquoted value run
            const std::size_t b = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' &&
                   line[i] != '=' && line[i] != '>' && line[i] != '"' &&
                   line[i] != '\'' && line[i] != '<') ++i;
            if (b == i) {  // the '=' itself
                Emit(out, i, i + 1, TokKind::Punct);
                ++i;
                continue;
            }
            std::size_t p = b;
            while (p > 0 &&
                   (line[p - 1] == ' ' || line[p - 1] == '\t')) --p;
            const bool value = p > 0 && line[p - 1] == '=';
            Emit(out, b, i,
                 value ? TokKind::String : TokKind::Type);
            continue;
        }
        // Markup.
        const char c = line[i];
        if (c == '<') {
            if (line.compare(i, 4, "<!--") == 0) {
                const auto close = line.find("-->", i + 4);
                if (close == std::string_view::npos) {
                    Emit(out, i, line.size(), TokKind::Comment);
                    st.mode = 1;
                    return;
                }
                Emit(out, i, close + 3, TokKind::Comment);
                i = close + 3;
                continue;
            }
            if (i + 1 < line.size() &&
                (line[i + 1] == '!' || line[i + 1] == '?')) {
                const auto close = line.find('>', i + 2);
                if (close == std::string_view::npos) {
                    Emit(out, i, line.size(), TokKind::Preproc);
                    st.mode = 2;
                    return;
                }
                Emit(out, i, close + 1, TokKind::Preproc);
                i = close + 1;
                continue;
            }
            std::size_t name_at = i + 1;
            bool closing = false;
            if (name_at < line.size() && line[name_at] == '/') {
                closing = true;
                ++name_at;
            }
            if (name_at < line.size() &&
                std::isalpha(static_cast<unsigned char>(
                    line[name_at]))) {
                const std::size_t nb = name_at;
                std::size_t ne = nb + 1;
                while (ne < line.size() &&
                       IsTagNameChar(static_cast<unsigned char>(
                           line[ne]))) ++ne;
                const std::string_view name = line.substr(nb, ne - nb);
                Emit(out, i, closing ? i + 2 : i + 1, TokKind::Punct);
                Emit(out, nb, ne, TokKind::Keyword);
                i = ne;
                if (closing) {
                    st.mode = 10;
                } else {
                    std::string low;
                    low.reserve(name.size());
                    for (const char ch : name) {
                        low.push_back(static_cast<char>(std::tolower(
                            static_cast<unsigned char>(ch))));
                    }
                    st.mode = low == "script" ? 11
                              : low == "style" ? 12
                                               : 10;
                }
                continue;
            }
            Emit(out, i, i + 1, TokKind::Punct);
            ++i;
            continue;
        }
        if (c == '&') {  // entity ref &name; / &#123;
            std::size_t e = i + 1;
            if (e < line.size() && line[e] == '#') ++e;
            while (e < line.size() &&
                   std::isalnum(static_cast<unsigned char>(line[e]))) ++e;
            if (e < line.size() && line[e] == ';' && e > i + 1) {
                Emit(out, i, e + 1, TokKind::Preproc);
                i = e + 1;
                continue;
            }
            Emit(out, i, i + 1, TokKind::Punct);
            ++i;
            continue;
        }
        // Text run.
        const std::size_t b = i;
        while (i < line.size() && line[i] != '<' && line[i] != '&') ++i;
        Emit(out, b, i, TokKind::Plain);
    }
}

// ---- palette ------------------------------------------------------------------

struct Palette {
    std::array<XER::UI::Theme::Color,
               static_cast<int>(TokKind::kCount)> colors;
};

Palette MakePalette(const XER::UI::Theme::Tokens& tokens) {
    Palette p;
    auto& c = p.colors;
    const float lum = tokens.background.r * 0.299f +
                      tokens.background.g * 0.587f +
                      tokens.background.b * 0.114f;
    const bool dark = lum < 0.5f;
    if (dark) {
        // Plain text must read near-white — when the theme's foreground
        // is dim, fall back to VS Code Dark+'s default #D4D4D4.
        const float fl = tokens.foreground.r * 0.299f +
                         tokens.foreground.g * 0.587f +
                         tokens.foreground.b * 0.114f;
        c[static_cast<int>(TokKind::Plain)] =
            fl >= 0.72f ? tokens.foreground
                        : XER::UI::Theme::Color{0.83f, 0.83f, 0.83f,
                                                1.f};
        // VS Code Dark+ inspired, tuned to sit beside the IDE theme.
        c[static_cast<int>(TokKind::Keyword)] = {0.337f, 0.612f, 0.839f, 1.f};
        c[static_cast<int>(TokKind::Type)] = {0.306f, 0.788f, 0.690f, 1.f};
        c[static_cast<int>(TokKind::String)] = {0.808f, 0.569f, 0.471f, 1.f};
        c[static_cast<int>(TokKind::Comment)] = {0.416f, 0.596f, 0.333f, 1.f};
        c[static_cast<int>(TokKind::Number)] = {0.710f, 0.808f, 0.659f, 1.f};
        c[static_cast<int>(TokKind::Preproc)] = {0.773f, 0.525f, 0.753f, 1.f};
        c[static_cast<int>(TokKind::Punct)] = {0.780f, 0.780f, 0.780f, 1.f};
    } else {
        c[static_cast<int>(TokKind::Plain)] = tokens.foreground;
        c[static_cast<int>(TokKind::Keyword)] = {0.0f, 0.0f, 0.667f, 1.f};
        c[static_cast<int>(TokKind::Type)] = {0.169f, 0.478f, 0.435f, 1.f};
        c[static_cast<int>(TokKind::String)] = {0.639f, 0.082f, 0.082f, 1.f};
        c[static_cast<int>(TokKind::Comment)] = {0.0f, 0.5f, 0.0f, 1.f};
        c[static_cast<int>(TokKind::Number)] = {0.035f, 0.443f, 0.294f, 1.f};
        c[static_cast<int>(TokKind::Preproc)] = {0.498f, 0.212f, 0.608f, 1.f};
        c[static_cast<int>(TokKind::Punct)] = {0.2f, 0.2f, 0.2f, 1.f};
    }
    return p;
}

}  // namespace

// ---- public headless API ------------------------------------------------------

EditorLanguage LanguageForPath(const std::string& rel_path) {
    const auto dot = rel_path.find_last_of('.');
    if (dot == std::string::npos) return EditorLanguage::Plain;
    std::string ext = rel_path.substr(dot + 1);
    for (char& ch : ext) ch = static_cast<char>(std::tolower(
                             static_cast<unsigned char>(ch)));
    if (ext == "cc" || ext == "cpp" || ext == "cxx" || ext == "c" ||
        ext == "h" || ext == "hh" || ext == "hpp" || ext == "hxx") {
        return EditorLanguage::Cpp;
    }
    if (ext == "js" || ext == "mjs" || ext == "cjs") {
        return EditorLanguage::JavaScript;
    }
    if (ext == "toml") return EditorLanguage::Toml;
    if (ext == "json") return EditorLanguage::Json;
    if (ext == "html" || ext == "htm" || ext == "xml" ||
        ext == "xhtml" || ext == "svg") {
        return EditorLanguage::Html;
    }
    if (ext == "css") return EditorLanguage::Css;
    return EditorLanguage::Plain;
}

const char* LanguageName(EditorLanguage lang) {
    switch (lang) {
        case EditorLanguage::Cpp: return "C++";
        case EditorLanguage::JavaScript: return "JavaScript";
        case EditorLanguage::Toml: return "TOML";
        case EditorLanguage::Json: return "JSON";
        case EditorLanguage::Html: return "HTML";
        case EditorLanguage::Css: return "CSS";
        default: return "Plain";
    }
}

std::vector<HighlightSpan> TokenizeLine(EditorLanguage lang,
                                        std::string_view line,
                                        LineLexState& state) {
    std::vector<HighlightSpan> out;
    switch (lang) {
        case EditorLanguage::Cpp:
            TokenizeCppLine(line, out, state);
            break;
        case EditorLanguage::JavaScript:
            TokenizeJsLine(line, out, state);
            break;
        case EditorLanguage::Toml:
            TokenizeTomlLine(line, out, state);
            break;
        case EditorLanguage::Json:
            TokenizeJsonLine(line, out, state);
            break;
        case EditorLanguage::Html:
            TokenizeHtmlLine(line, out, state);
            break;
        case EditorLanguage::Css:
            TokenizeCssLine(line, out, state);
            break;
        default:
            Emit(out, 0, line.size(), TokKind::Plain);
            break;
    }
    return out;
}

std::vector<std::vector<HighlightSpan>> TokenizeDocument(
    EditorLanguage lang, const std::vector<std::string>& lines) {
    std::vector<std::vector<HighlightSpan>> out(lines.size());
    LineLexState st;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out[i] = TokenizeLine(lang, lines[i], st);
    }
    return out;
}

// ---- headless editing helpers ---------------------------------------------------

std::string DetectIndentUnit(const std::vector<std::string>& lines,
                             int spaces) {
    const std::size_t n = std::min<std::size_t>(lines.size(), 200);
    for (std::size_t i = 0; i < n; ++i) {
        if (!lines[i].empty() && lines[i].front() == '\t') {
            return "\t";
        }
    }
    return std::string(static_cast<std::size_t>(std::max(1, spaces)),
                       ' ');
}

AutoIndent AutoIndentForNewline(std::string_view line,
                                std::size_t caret_col,
                                EditorLanguage lang,
                                std::string_view indent_unit) {
    AutoIndent out;
    caret_col = std::min(caret_col, line.size());
    const std::string_view left = line.substr(0, caret_col);
    const std::string_view right = line.substr(caret_col);
    // Base indent = the line's leading whitespace up to the caret (the
    // whole left half when the caret sits inside the indent itself).
    const auto ws_end = left.find_first_not_of(" \t");
    const std::string_view base =
        left.substr(0, ws_end == std::string_view::npos
                           ? left.size()
                           : ws_end);
    out.indent.assign(base);
    const auto ltrim = left.find_last_not_of(" \t");
    const char last =
        ltrim == std::string_view::npos ? '\0' : left[ltrim];
    bool opens =
        last == '{' || last == '(' || last == '[';
    if (!opens && lang == EditorLanguage::Html && last == '>') {
        // A line ending in a non-self-closing open tag indents: ends
        // with '>' but not '/>' or a '</close>' tag.
        const std::string_view lt = left.substr(0, ltrim + 1);
        if (lt.size() < 2 || lt[lt.size() - 2] != '/') {
            // The last '<' must open a tag (alpha next) — '</x>' and
            // text ending in 'a > b' do not indent.
            const auto open_lt = lt.find_last_of('<');
            if (open_lt != std::string_view::npos &&
                open_lt + 1 < lt.size() &&
                std::isalpha(static_cast<unsigned char>(
                    lt[open_lt + 1]))) {
                opens = true;
            }
        }
    }
    if (opens) out.indent += indent_unit;
    // Caret between a matching pair (`{|}`): split into the standard
    // 3-line block — the tail lands on a line at the BASE indent.
    if (last == '{' || last == '(' || last == '[') {
        const auto rfirst = right.find_first_not_of(" \t");
        if (rfirst != std::string_view::npos &&
            right[rfirst] == PairedClose(last)) {
            out.extra_line = true;
            out.closing.assign(base);
        }
    }
    return out;
}

char PairedClose(char open) {
    switch (open) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '"': return '"';
        case '\'': return '\'';
        default: return '\0';
    }
}

bool AutoPairOk(std::string_view line, std::size_t caret_col) {
    if (caret_col >= line.size()) return true;
    const unsigned char next =
        static_cast<unsigned char>(line[caret_col]);
    return !IsIdent(next);
}

PairPlan PlanPairInput(std::string_view line, std::size_t caret_col,
                       char c) {
    PairPlan plan;
    const char close = PairedClose(c);
    const bool is_quote = c == '"' || c == '\'';
    const bool is_closer =
        c == ')' || c == ']' || c == '}' || is_quote;
    if (!is_closer && close == '\0') return plan;
    caret_col = std::min(caret_col, line.size());
    const char next =
        caret_col < line.size() ? line[caret_col] : '\0';
    if (is_closer && next == c) {
        plan.overtype = true;
        return plan;
    }
    if (close != '\0' && AutoPairOk(line, caret_col)) {
        plan.pair = true;
    }
    return plan;
}

BackspacePlan PlanBackspace(std::string_view line, std::size_t caret_col,
                            int tab_width) {
    BackspacePlan plan;
    caret_col = std::min(caret_col, line.size());
    if (caret_col == 0) {
        plan.join_prev = true;
        return plan;
    }
    // Caret between an empty auto-pair (`(|)`) deletes both sides.
    if (caret_col < line.size() &&
        PairedClose(line[caret_col - 1]) == line[caret_col]) {
        plan.begin = caret_col - 1;
        plan.end = caret_col + 1;
        return plan;
    }
    // Inside leading whitespace a lone '\t' goes one char, spaces jump
    // back to the previous tab stop.
    const auto ws_end = line.find_first_not_of(" \t");
    const std::size_t lead =
        ws_end == std::string_view::npos ? line.size() : ws_end;
    if (caret_col <= lead) {
        if (line[caret_col - 1] == '\t') {
            plan.begin = caret_col - 1;
        } else {
            const std::size_t tw =
                static_cast<std::size_t>(std::max(1, tab_width));
            plan.begin = (caret_col - 1) / tw * tw;
        }
        plan.end = caret_col;
        return plan;
    }
    const std::size_t prev = PrevCodePoint(line, caret_col);
    plan.begin = prev;
    plan.end = caret_col;
    return plan;
}

// ---- widget --------------------------------------------------------------------

CodeEditorWidget::CodeEditorWidget() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void CodeEditorWidget::SetText(std::string text) {
    // Normalize newlines; split into the line store.
    std::vector<std::string> lines;
    std::string cur;
    cur.reserve(text.size() + 1);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            lines.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    lines.push_back(std::move(cur));
    lines_ = std::move(lines);
    Retokenize();
    caret_line_ = caret_col_ = anchor_line_ = anchor_col_ = 0;
    preferred_col_ = -1;
    scroll_line_ = scroll_x_ = 0.0f;
    content_w_ = 0.0f;
    vbar_drag_ = hbar_drag_ = false;
    click_count_ = 0;
    undo_.clear();
    redo_.clear();
    Invalidate();
}

std::string CodeEditorWidget::GetText() const {
    std::string out;
    std::size_t total = 0;
    for (const auto& l : lines_) total += l.size() + 1;
    out.reserve(total);
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (i) out.push_back('\n');
        out += lines_[i];
    }
    return out;
}

void CodeEditorWidget::SetPlaceholder(std::string placeholder) {
    placeholder_ = std::move(placeholder);
    Invalidate();
}

void CodeEditorWidget::SetReadOnly(bool read_only) {
    read_only_ = read_only;
    SetFocusable(!read_only_);
    if (read_only_) SetFocused(false);
    Invalidate();
}

void CodeEditorWidget::SetLanguage(EditorLanguage lang) {
    if (language_ == lang) return;
    language_ = lang;
    Retokenize();
    Invalidate();
}

void CodeEditorWidget::SetOnChanged(
    std::function<void(const std::string&)> callback) {
    on_changed_ = std::move(callback);
}

void CodeEditorWidget::SetOnCaretChanged(
    std::function<void(int, int)> callback) {
    on_caret_ = std::move(callback);
}

void CodeEditorWidget::SetCaret(int line, int col) {
    caret_line_ = std::clamp(line, 0,
                             static_cast<int>(lines_.size()) - 1);
    caret_col_ = static_cast<int>(
        LineByteClamp(caret_line_, col));
    anchor_line_ = caret_line_;
    anchor_col_ = caret_col_;
    AfterCaretMove();
}

void CodeEditorWidget::GotoLine(int line, int col) {
    SetCaret(line, col);
    EnsureCaretVisible();
    Invalidate();
}

void CodeEditorWidget::SelectRange(int line, int col, int len) {
    line = std::clamp(line, 0, static_cast<int>(lines_.size()) - 1);
    col = static_cast<int>(LineByteClamp(line, col));
    anchor_line_ = line;
    anchor_col_ = col;
    caret_line_ = line;
    caret_col_ = static_cast<int>(
        LineByteClamp(line, col + std::max(0, len)));
    AfterCaretMove();
    EnsureCaretVisible();
    Invalidate();
}

bool CodeEditorWidget::HasSelection() const {
    return anchor_line_ != caret_line_ || anchor_col_ != caret_col_;
}

void CodeEditorWidget::InsertTextAtCaret(const std::string& text) {
    if (read_only_) return;
    PushUndo();
    DeleteSelection();
    // InsertText-equivalent on the line store.
    std::string_view rest = text;
    while (true) {
        const auto nl = rest.find('\n');
        std::string_view piece =
            nl == std::string_view::npos ? rest : rest.substr(0, nl);
        // Strip control chars except tab (tab -> 4 spaces).
        std::string clean;
        clean.reserve(piece.size());
        for (std::size_t k = 0; k < piece.size(); ++k) {
            const auto c = static_cast<unsigned char>(piece[k]);
            if (c == '\r') continue;
            if (c == '\t') { clean += "    "; continue; }
            if (c < 0x20u) continue;
            const std::size_t take =
                std::min(Utf8SeqLen(c), piece.size() - k);
            clean.append(piece, k, take);
            k += take - 1;
        }
        std::string& line = lines_[static_cast<std::size_t>(caret_line_)];
        const int col = static_cast<int>(
            LineByteClamp(caret_line_, caret_col_));
        line.insert(static_cast<std::size_t>(col), clean);
        caret_col_ = col + static_cast<int>(clean.size());
        if (nl == std::string_view::npos) break;
        // Split the line at the caret.
        std::string tail = line.substr(
            static_cast<std::size_t>(caret_col_));
        line.resize(static_cast<std::size_t>(caret_col_));
        lines_.insert(
            lines_.begin() + caret_line_ + 1, std::move(tail));
        ++caret_line_;
        caret_col_ = 0;
        rest = rest.substr(nl + 1);
    }
    anchor_line_ = caret_line_;
    anchor_col_ = caret_col_;
    coalesce_typing_ = false;
    Changed();
}

void CodeEditorWidget::SetSearchMatches(
    std::vector<SearchMatch> matches, int active) {
    matches_ = std::move(matches);
    active_match_ = active;
    Invalidate();
}

// ---- editing -------------------------------------------------------------------

void CodeEditorWidget::PushUndo() {
    // Coalesce consecutive typed characters into one undo step; every
    // other op starts a fresh entry.
    if (coalesce_typing_ && !undo_.empty()) return;
    undo_.push_back({lines_, caret_line_, caret_col_});
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
}

bool CodeEditorWidget::Undo() {
    if (undo_.empty()) return false;
    redo_.push_back({lines_, caret_line_, caret_col_});
    const UndoEntry e = undo_.back();
    undo_.pop_back();
    lines_ = e.lines;
    caret_line_ = e.caret_line;
    caret_col_ = e.caret_col;
    anchor_line_ = caret_line_;
    anchor_col_ = caret_col_;
    coalesce_typing_ = false;
    Changed();
    return true;
}

bool CodeEditorWidget::Redo() {
    if (redo_.empty()) return false;
    undo_.push_back({lines_, caret_line_, caret_col_});
    const UndoEntry e = redo_.back();
    redo_.pop_back();
    lines_ = e.lines;
    caret_line_ = e.caret_line;
    caret_col_ = e.caret_col;
    anchor_line_ = caret_line_;
    anchor_col_ = caret_col_;
    coalesce_typing_ = false;
    Changed();
    return true;
}

std::pair<int, int> CodeEditorWidget::SelFrom() const {
    if (anchor_line_ != caret_line_) {
        return anchor_line_ < caret_line_
                   ? std::pair{anchor_line_, anchor_col_}
                   : std::pair{caret_line_, caret_col_};
    }
    return anchor_col_ <= caret_col_
               ? std::pair{anchor_line_, anchor_col_}
               : std::pair{caret_line_, caret_col_};
}
std::pair<int, int> CodeEditorWidget::SelTo() const {
    if (anchor_line_ != caret_line_) {
        return anchor_line_ > caret_line_
                   ? std::pair{anchor_line_, anchor_col_}
                   : std::pair{caret_line_, caret_col_};
    }
    return anchor_col_ >= caret_col_
               ? std::pair{anchor_line_, anchor_col_}
               : std::pair{caret_line_, caret_col_};
}

std::string CodeEditorWidget::SelectedText() const {
    if (!HasSelection()) return {};
    const auto [fl, fc] = SelFrom();
    const auto [tl, tc] = SelTo();
    if (fl == tl) {
        return lines_[static_cast<std::size_t>(fl)].substr(
            static_cast<std::size_t>(fc),
            static_cast<std::size_t>(tc - fc));
    }
    std::string out = lines_[static_cast<std::size_t>(fl)].substr(
        static_cast<std::size_t>(fc));
    for (int l = fl + 1; l < tl; ++l) {
        out.push_back('\n');
        out += lines_[static_cast<std::size_t>(l)];
    }
    out.push_back('\n');
    out += lines_[static_cast<std::size_t>(tl)].substr(
        0, static_cast<std::size_t>(tc));
    return out;
}

bool CodeEditorWidget::DeleteSelection() {
    if (!HasSelection()) return false;
    const auto [fl, fc] = SelFrom();
    const auto [tl, tc] = SelTo();
    if (fl == tl) {
        lines_[static_cast<std::size_t>(fl)].erase(
            static_cast<std::size_t>(fc),
            static_cast<std::size_t>(tc - fc));
    } else {
        std::string merged =
            lines_[static_cast<std::size_t>(fl)].substr(
                0, static_cast<std::size_t>(fc)) +
            lines_[static_cast<std::size_t>(tl)].substr(
                static_cast<std::size_t>(tc));
        lines_[static_cast<std::size_t>(fl)] = std::move(merged);
        lines_.erase(lines_.begin() + fl + 1,
                     lines_.begin() + tl + 1);
    }
    caret_line_ = anchor_line_ = fl;
    caret_col_ = anchor_col_ = fc;
    return true;
}

void CodeEditorWidget::InsertBytes(std::string_view bytes) {
    std::string& line = lines_[static_cast<std::size_t>(caret_line_)];
    const int col =
        static_cast<int>(LineByteClamp(caret_line_, caret_col_));
    line.insert(static_cast<std::size_t>(col), bytes);
    caret_col_ = col + static_cast<int>(bytes.size());
    anchor_line_ = caret_line_;
    anchor_col_ = caret_col_;
}

void CodeEditorWidget::InsertNewline() {
    std::string& line = lines_[static_cast<std::size_t>(caret_line_)];
    const int col =
        static_cast<int>(LineByteClamp(caret_line_, caret_col_));
    const AutoIndent ai = AutoIndentForNewline(
        line, static_cast<std::size_t>(col), language_, IndentUnit());
    std::string tail = line.substr(static_cast<std::size_t>(col));
    line.resize(static_cast<std::size_t>(col));
    if (ai.extra_line) {
        // `{|}` → the standard 3-line block with the caret on the
        // indented middle line and the tail closing at base indent.
        lines_.insert(lines_.begin() + caret_line_ + 1, ai.indent);
        lines_.insert(lines_.begin() + caret_line_ + 2,
                      ai.closing + tail);
        ++caret_line_;
        caret_col_ = static_cast<int>(ai.indent.size());
    } else {
        lines_.insert(lines_.begin() + caret_line_ + 1,
                      ai.indent + tail);
        ++caret_line_;
        caret_col_ = static_cast<int>(ai.indent.size());
    }
    anchor_line_ = caret_line_;
    anchor_col_ = caret_col_;
}

void CodeEditorWidget::Backspace() {
    if (DeleteSelection()) return;
    std::string& line = lines_[static_cast<std::size_t>(caret_line_)];
    const int col =
        static_cast<int>(LineByteClamp(caret_line_, caret_col_));
    const BackspacePlan plan =
        PlanBackspace(line, static_cast<std::size_t>(col));
    if (plan.join_prev) {
        if (caret_line_ > 0) {
            const int prev_len = static_cast<int>(
                lines_[static_cast<std::size_t>(caret_line_ - 1)]
                    .size());
            lines_[static_cast<std::size_t>(caret_line_ - 1)] += line;
            lines_.erase(lines_.begin() + caret_line_);
            --caret_line_;
            caret_col_ = prev_len;
        }
    } else {
        line.erase(plan.begin, plan.end - plan.begin);
        caret_col_ = static_cast<int>(plan.begin);
    }
    anchor_line_ = caret_line_;
    anchor_col_ = caret_col_;
}

void CodeEditorWidget::DeleteForward() {
    if (DeleteSelection()) return;
    std::string& line = lines_[static_cast<std::size_t>(caret_line_)];
    const int col =
        static_cast<int>(LineByteClamp(caret_line_, caret_col_));
    if (col < static_cast<int>(line.size())) {
        const auto next =
            NextCodePoint(line, static_cast<std::size_t>(col));
        line.erase(static_cast<std::size_t>(col),
                   next - static_cast<std::size_t>(col));
    } else if (caret_line_ + 1 <
               static_cast<int>(lines_.size())) {
        line += lines_[static_cast<std::size_t>(caret_line_ + 1)];
        lines_.erase(lines_.begin() + caret_line_ + 1);
    }
    anchor_line_ = caret_line_;
    anchor_col_ = caret_col_;
}

std::string CodeEditorWidget::IndentUnit() const {
    return DetectIndentUnit(lines_);
}

void CodeEditorWidget::IndentLines(bool dedent) {
    const auto [fl, fc] = SelFrom();
    const auto [tl0, tc] = SelTo();
    int tl = tl0;
    // A selection ending exactly at a line start doesn't touch that
    // line (the usual "select whole lines" convention).
    if (HasSelection() && tc == 0 && tl > fl) --tl;
    const std::string unit = IndentUnit();
    std::vector<int> delta(static_cast<std::size_t>(tl - fl + 1), 0);
    for (int l = fl; l <= tl; ++l) {
        std::string& s = lines_[static_cast<std::size_t>(l)];
        if (!dedent) {
            s.insert(0, unit);
            delta[static_cast<std::size_t>(l - fl)] =
                static_cast<int>(unit.size());
        } else if (!s.empty() && s.front() == '\t') {
            s.erase(0, 1);
            delta[static_cast<std::size_t>(l - fl)] = -1;
        } else {
            int n = 0;
            while (n < static_cast<int>(s.size()) && n < 4 &&
                   s[static_cast<std::size_t>(n)] == ' ') {
                ++n;
            }
            if (n > 0) {
                s.erase(0, static_cast<std::size_t>(n));
                delta[static_cast<std::size_t>(l - fl)] = -n;
            }
        }
    }
    // Keep the selection anchored on the same text.
    if (caret_line_ >= fl && caret_line_ <= tl) {
        caret_col_ = std::max(
            0, caret_col_ +
                   delta[static_cast<std::size_t>(caret_line_ - fl)]);
    }
    if (anchor_line_ >= fl && anchor_line_ <= tl) {
        anchor_col_ = std::max(
            0, anchor_col_ +
                   delta[static_cast<std::size_t>(anchor_line_ - fl)]);
    }
}

void CodeEditorWidget::ToggleComment() {
    const auto [fl, fc] = SelFrom();
    const auto [tl, tc] = SelTo();
    (void)fc;
    (void)tc;
    const auto non_ws = [](const std::string& s) {
        return s.find_first_not_of(" \t");
    };
    if (language_ == EditorLanguage::Css ||
        language_ == EditorLanguage::Html) {
        const std::string open =
            language_ == EditorLanguage::Css ? "/*" : "<!--";
        const std::string close =
            language_ == EditorLanguage::Css ? "*/" : "-->";
        // Unwrap only when every touched non-blank line is wrapped.
        bool all_wrapped = true;
        for (int l = fl; l <= tl; ++l) {
            const std::string& s = lines_[static_cast<std::size_t>(l)];
            const auto b = non_ws(s);
            if (b == std::string::npos) continue;
            const auto e = s.find_last_not_of(" \t");
            if (!(e + 1 >= open.size() + close.size() &&
                  s.compare(b, open.size(), open) == 0 &&
                  s.compare(e + 1 - close.size(), close.size(), close) ==
                      0)) {
                all_wrapped = false;
            }
        }
        for (int l = fl; l <= tl; ++l) {
            std::string& s = lines_[static_cast<std::size_t>(l)];
            const auto b = non_ws(s);
            if (b == std::string::npos) continue;
            const auto e = s.find_last_not_of(" \t");
            if (all_wrapped) {
                s.erase(e + 1 - close.size(), close.size());
                s.erase(b, open.size());
            } else {
                s.insert(e + 1, close);
                s.insert(b, open + " ");
            }
        }
        return;
    }
    const std::string marker =
        language_ == EditorLanguage::Toml ? "#" : "//";
    bool all_commented = true;
    for (int l = fl; l <= tl; ++l) {
        const std::string& s = lines_[static_cast<std::size_t>(l)];
        const auto b = non_ws(s);
        if (b == std::string::npos) continue;
        if (s.compare(b, marker.size(), marker) != 0) {
            all_commented = false;
        }
    }
    for (int l = fl; l <= tl; ++l) {
        std::string& s = lines_[static_cast<std::size_t>(l)];
        const auto b = non_ws(s);
        if (b == std::string::npos) continue;
        if (all_commented) {
            s.erase(b, marker.size());
            if (b < s.size() && s[b] == ' ') s.erase(b, 1);
        } else {
            s.insert(b, marker + " ");
        }
    }
}

void CodeEditorWidget::WrapSelection(char open) {
    const char close = PairedClose(open);
    if (close == '\0') return;
    const auto [fl, fc] = SelFrom();
    const auto [tl, tc] = SelTo();
    // Insert the closer first (it sits at the larger offset on the
    // shared-line case), then the opener.
    lines_[static_cast<std::size_t>(tl)].insert(
        static_cast<std::size_t>(tc), 1, close);
    lines_[static_cast<std::size_t>(fl)].insert(
        static_cast<std::size_t>(fc), 1, open);
    anchor_line_ = fl;
    anchor_col_ = fc + 1;
    caret_line_ = tl;
    caret_col_ = tc + 1;
}

TokKind CodeEditorWidget::TokenKindAtCaret() const {
    if (caret_line_ < 0 ||
        caret_line_ >= static_cast<int>(spans_.size())) {
        return TokKind::Plain;
    }
    const auto& ss = spans_[static_cast<std::size_t>(caret_line_)];
    for (const HighlightSpan& sp : ss) {
        if (caret_col_ >= static_cast<int>(sp.begin) &&
            caret_col_ < static_cast<int>(sp.end)) {
            return sp.kind;
        }
    }
    // A caret sitting at end-of-line belongs to the last span's token
    // (typing inside `// comment|` must still read "comment").
    if (!ss.empty() &&
        caret_col_ == static_cast<int>(ss.back().end) &&
        caret_line_ < static_cast<int>(lines_.size()) &&
        caret_col_ ==
            static_cast<int>(
                lines_[static_cast<std::size_t>(caret_line_)].size())) {
        return ss.back().kind;
    }
    return TokKind::Plain;
}

bool CodeEditorWidget::HandlePairInput(char c) {
    std::string& line = lines_[static_cast<std::size_t>(caret_line_)];
    const int col =
        static_cast<int>(LineByteClamp(caret_line_, caret_col_));
    if (HasSelection()) {
        if (PairedClose(c) != '\0') {
            coalesce_typing_ = false;
            PushUndo();
            WrapSelection(c);
            return true;
        }
        return false;
    }
    const PairPlan plan =
        PlanPairInput(line, static_cast<std::size_t>(col), c);
    if (plan.overtype) {
        caret_col_ = col + 1;
        anchor_line_ = caret_line_;
        anchor_col_ = caret_col_;
        coalesce_typing_ = false;
        return true;
    }
    if (!plan.pair) return false;
    // No pairing inside comments/strings (cheap context check via the
    // existing tokenizer spans).
    const TokKind kind = TokenKindAtCaret();
    if (kind == TokKind::Comment || kind == TokKind::String) {
        return false;
    }
    // Own undo step: break the typing coalescing and snapshot now.
    coalesce_typing_ = false;
    PushUndo();
    const char close = PairedClose(c);
    const std::string pair = std::string(1, c) + close;
    line.insert(static_cast<std::size_t>(col), pair);
    caret_col_ = col + 1;
    anchor_line_ = caret_line_;
    anchor_col_ = caret_col_;
    return true;
}

bool CodeEditorWidget::HandleShortcut(KeyCode key, bool shift) {
    switch (key) {
        case KeyCode::A:
            anchor_line_ = anchor_col_ = 0;
            caret_line_ = static_cast<int>(lines_.size()) - 1;
            caret_col_ = static_cast<int>(lines_.back().size());
            AfterCaretMove();
            Invalidate();
            return true;
        case KeyCode::C:
        case KeyCode::X:
            if (!HasSelection()) return true;
            XER::Content::Clipboard::SetText(SelectedText());
            if (key == KeyCode::X && !read_only_) {
                PushUndo();
                DeleteSelection();
                coalesce_typing_ = false;
                Changed();
            }
            return true;
        case KeyCode::V: {
            if (read_only_) return true;
            std::string clip;
            if (!XER::Content::Clipboard::GetText(clip)) return true;
            InsertTextAtCaret(clip);
            return true;
        }
        case KeyCode::Z:
            if (!read_only_) (void)(shift ? Redo() : Undo());
            return true;
        case KeyCode::Y:
            if (!read_only_) (void)Redo();
            return true;
        case KeyCode::Slash:
            if (read_only_) return true;
            PushUndo();
            ToggleComment();
            coalesce_typing_ = false;
            Changed();
            return true;
        default:
            return false;
    }
}

void CodeEditorWidget::MoveHorizontal(int dir, bool extend) {
    if (HasSelection() && !extend) {
        const auto [l, c] =
            dir < 0 ? SelFrom() : SelTo();
        caret_line_ = l;
        caret_col_ = c;
    } else {
        std::string& line =
            lines_[static_cast<std::size_t>(caret_line_)];
        if (dir < 0) {
            if (caret_col_ > 0) {
                caret_col_ = static_cast<int>(PrevCodePoint(
                    line, static_cast<std::size_t>(caret_col_)));
            } else if (caret_line_ > 0) {
                --caret_line_;
                caret_col_ = static_cast<int>(
                    lines_[static_cast<std::size_t>(caret_line_)]
                        .size());
            }
        } else {
            if (caret_col_ < static_cast<int>(line.size())) {
                caret_col_ = static_cast<int>(NextCodePoint(
                    line, static_cast<std::size_t>(caret_col_)));
            } else if (caret_line_ + 1 <
                       static_cast<int>(lines_.size())) {
                ++caret_line_;
                caret_col_ = 0;
            }
        }
    }
    if (!extend) {
        anchor_line_ = caret_line_;
        anchor_col_ = caret_col_;
    }
    preferred_col_ = -1;
    AfterCaretMove();
    Invalidate();
}

void CodeEditorWidget::MoveVertical(int dir, bool extend) {
    if (preferred_col_ < 0) preferred_col_ = caret_col_;
    caret_line_ = std::clamp(caret_line_ + dir, 0,
                             static_cast<int>(lines_.size()) - 1);
    caret_col_ = static_cast<int>(
        LineByteClamp(caret_line_, preferred_col_));
    if (!extend) {
        anchor_line_ = caret_line_;
        anchor_col_ = caret_col_;
    }
    AfterCaretMove();
    Invalidate();
}

void CodeEditorWidget::MoveLineStartEnd(bool end, bool extend) {
    const std::string& line =
        lines_[static_cast<std::size_t>(caret_line_)];
    if (!end) {
        // Home: first non-ws char, or col 0 when already there.
        const auto nb = line.find_first_not_of(" \t");
        const int target =
            nb == std::string::npos ? 0 : static_cast<int>(nb);
        caret_col_ = (caret_col_ == target) ? 0 : target;
    } else {
        caret_col_ = static_cast<int>(line.size());
    }
    if (!extend) {
        anchor_line_ = caret_line_;
        anchor_col_ = caret_col_;
    }
    preferred_col_ = -1;
    AfterCaretMove();
    Invalidate();
}

void CodeEditorWidget::MovePage(int dir, bool extend) {
    caret_line_ = std::clamp(
        caret_line_ + dir * std::max(1, VisibleLines() - 1), 0,
        static_cast<int>(lines_.size()) - 1);
    caret_col_ = static_cast<int>(
        LineByteClamp(caret_line_, caret_col_));
    if (!extend) {
        anchor_line_ = caret_line_;
        anchor_col_ = caret_col_;
    }
    AfterCaretMove();
    Invalidate();
}

std::size_t CodeEditorWidget::LineByteClamp(int line, int col) const {
    const std::string& s = lines_[static_cast<std::size_t>(
        std::clamp(line, 0, static_cast<int>(lines_.size()) - 1))];
    std::size_t c = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::max(0, col)), 0, s.size());
    if (c > 0 && c < s.size() &&
        IsContinuation(static_cast<unsigned char>(s[c]))) {
        c = PrevCodePoint(s, c);
    }
    return c;
}

void CodeEditorWidget::ClampCaret() {
    caret_line_ = std::clamp(caret_line_, 0,
                             static_cast<int>(lines_.size()) - 1);
    caret_col_ =
        static_cast<int>(LineByteClamp(caret_line_, caret_col_));
}

void CodeEditorWidget::AfterCaretMove() {
    ClampCaret();
    EnsureCaretVisible();
    if (on_caret_) on_caret_(caret_line_, caret_col_);
}

void CodeEditorWidget::EnsureCaretVisible() {
    const int vis = VisibleLines();
    if (caret_line_ < static_cast<int>(scroll_line_)) {
        scroll_line_ = static_cast<float>(caret_line_);
    } else if (caret_line_ >= scroll_line_ + vis) {
        scroll_line_ =
            static_cast<float>(caret_line_ - vis + 1);
    }
    scroll_line_ = std::clamp(
        scroll_line_, 0.0f,
        std::max(0.0f, static_cast<float>(lines_.size()) - 1.0f));
    // Horizontal: approximate the caret pixel column (no renderer at
    // event time); Render() refines with exact measurement.
    const Rect b = GetBounds();
    const float view_w = b.width - TextX() - pad_;
    if (view_w > 0.0f) {
        const float px =
            static_cast<float>(caret_col_) * avg_char_w_;
        if (px < scroll_x_) {
            scroll_x_ = px;
        } else if (px > scroll_x_ + view_w - 10.0f) {
            scroll_x_ = px - view_w + 10.0f;
        }
        scroll_x_ = std::clamp(scroll_x_, 0.0f, MaxScrollX() + 40.0f);
    }
}

int CodeEditorWidget::VisibleLines() const {
    const Rect b = GetBounds();
    if (b.height <= 0.0f || line_height_ <= 0.0f) return 1;
    return std::max(
        1, static_cast<int>((b.height - 2.0f * pad_ - hbar_h_) /
                            line_height_));
}

float CodeEditorWidget::MaxScrollX() const {
    const Rect b = GetBounds();
    const float view_w = b.width - TextX() - pad_;
    return std::max(0.0f, content_w_ - view_w + 2.0f * pad_);
}

float CodeEditorWidget::MaxScrollY() const {
    return std::max(
        0.0f, static_cast<float>(lines_.size()) -
                  static_cast<float>(VisibleLines()) + 2.0f);
}

void CodeEditorWidget::ScrollVTo(float y) {
    const float span = vbar_track_.height - vbar_thumb_.height;
    if (span <= 0.0f) {
        scroll_line_ = 0.0f;
        return;
    }
    const float frac =
        std::clamp((y - vbar_track_.y - grab_off_) / span, 0.0f, 1.0f);
    scroll_line_ = frac * MaxScrollY();
}

void CodeEditorWidget::ScrollHTo(float x) {
    const float span = hbar_track_.width - hbar_thumb_.width;
    if (span <= 0.0f) {
        scroll_x_ = 0.0f;
        return;
    }
    const float frac =
        std::clamp((x - hbar_track_.x - grab_off_) / span, 0.0f, 1.0f);
    scroll_x_ = frac * MaxScrollX();
}

void CodeEditorWidget::SelectWordAt(int line, int col) {
    const std::string& s = lines_[static_cast<std::size_t>(line)];
    const auto word_char = [](unsigned char c) {
        // [A-Za-z0-9_$] plus path-ish separators so a double click on
        // "src/a.b" selects the whole run.
        return std::isalnum(c) || c == '_' || c == '$' || c == '/' ||
               c == '\\' || c == ':' || c == '.' || c == '~' ||
               c == '%' || c == '-' || c >= 0x80;
    };
    std::size_t c = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, col)), s.size());
    if (c == s.size() && c > 0) --c;
    if (s.empty() ||
        !word_char(static_cast<unsigned char>(s[c]))) {
        anchor_line_ = caret_line_ = line;
        anchor_col_ = caret_col_ = static_cast<int>(c);
        return;
    }
    std::size_t b = c, e = c + 1;
    while (b > 0 && word_char(static_cast<unsigned char>(s[b - 1])))
        --b;
    while (e < s.size() &&
           word_char(static_cast<unsigned char>(s[e])))
        ++e;
    anchor_line_ = caret_line_ = line;
    anchor_col_ = static_cast<int>(b);
    caret_col_ = static_cast<int>(e);
}

void CodeEditorWidget::SelectLineAt(int line) {
    line = std::clamp(line, 0, static_cast<int>(lines_.size()) - 1);
    anchor_line_ = caret_line_ = line;
    anchor_col_ = 0;
    caret_col_ = static_cast<int>(
        lines_[static_cast<std::size_t>(line)].size());
}

void CodeEditorWidget::Retokenize() {
    spans_ = TokenizeDocument(language_, lines_);
    // Track the longest line by bytes — Render() measures just that
    // one to estimate the scrollable content width.
    longest_line_ = 0;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].size() > lines_[longest_line_].size()) {
            longest_line_ = i;
        }
    }
}

void CodeEditorWidget::Changed() {
    Retokenize();
    AfterCaretMove();
    Invalidate();
    if (on_changed_) on_changed_(GetText());
}

// ---- hit mapping ------------------------------------------------------------------

std::pair<int, int> CodeEditorWidget::PositionAt(
    Point p, XER::UI::Rendering::Renderer& renderer) const {
    const Rect b = GetBounds();
    const int row = std::clamp(
        static_cast<int>((p.y - b.y - pad_) / line_height_ +
                         scroll_line_),
        0, static_cast<int>(lines_.size()) - 1);
    const std::string& line = lines_[static_cast<std::size_t>(row)];
    const float local_x = p.x - (b.x + TextX()) + scroll_x_;
    int col = static_cast<int>(line.size());
    if (local_x <= 0.0f) {
        col = 0;
    } else {
        for (std::size_t q = 0; q <= line.size();) {
            const float w = renderer.MeasureText(
                std::string_view(line).substr(0, q),
                {XER::UI::Theme::Color{}, font_size_, 400,
                 /*monospace=*/true}).width;
            if (w > local_x) {
                col = static_cast<int>(
                    q == 0 ? 0 : PrevCodePoint(line, q));
                break;
            }
            if (q == line.size()) break;
            q = NextCodePoint(line, q);
        }
    }
    return {row, col};
}

float CodeEditorWidget::ColumnWidth(
    XER::UI::Rendering::Renderer& renderer, int line, int col) const {
    const std::string& s = lines_[static_cast<std::size_t>(line)];
    const std::size_t c = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, col)), s.size());
    return renderer.MeasureText(std::string_view(s).substr(0, c),
                                {XER::UI::Theme::Color{}, font_size_,
                                 400, /*monospace=*/true})
        .width;
}

void CodeEditorWidget::ResolveClick(Point position, bool extend,
                                    XER::UI::Rendering::Renderer&
                                        renderer) {
    const auto [row, col] = PositionAt(position, renderer);
    caret_line_ = row;
    caret_col_ = col;
    if (!extend) {
        anchor_line_ = row;
        anchor_col_ = col;
    }
    if (pending_select_ == 1) {
        SelectWordAt(row, col);
    } else if (pending_select_ == 2) {
        SelectLineAt(row);
    }
    pending_select_ = 0;
    preferred_col_ = -1;
    AfterCaretMove();
}

// ---- events ------------------------------------------------------------------------

bool CodeEditorWidget::HandleEvent(const Event& event) {
    if (!IsEnabled()) return false;
    // Keys only apply while focused; pointer events land regardless so
    // the click can claim focus first.
    if (event.type != Event::Type::MouseDown &&
        event.type != Event::Type::MouseMove &&
        event.type != Event::Type::MouseUp &&
        event.type != Event::Type::MouseWheel &&
        !IsFocused()) {
        return false;
    }
    switch (event.type) {
        case Event::Type::MouseDown: {
            if (event.mouse_button != MouseButton::Left) return false;
            // Scrollbars first — they overlay the text lanes.
            if (vbar_visible_ && vbar_track_.Contains(event.position)) {
                vbar_drag_ = true;
                if (vbar_thumb_.Contains(event.position)) {
                    grab_off_ = event.position.y - vbar_thumb_.y;
                } else {
                    // Click on the track: center the thumb and keep
                    // dragging from there.
                    grab_off_ = vbar_thumb_.height * 0.5f;
                    ScrollVTo(event.position.y);
                }
                Invalidate();
                return true;
            }
            if (hbar_visible_ && hbar_track_.Contains(event.position)) {
                hbar_drag_ = true;
                if (hbar_thumb_.Contains(event.position)) {
                    grab_off_ = event.position.x - hbar_thumb_.x;
                } else {
                    grab_off_ = hbar_thumb_.width * 0.5f;
                    ScrollHTo(event.position.x);
                }
                Invalidate();
                return true;
            }
            // Click cadence for double (word) / triple (line) click.
            const auto now = std::chrono::steady_clock::now();
            const auto dt = std::chrono::duration_cast<
                std::chrono::milliseconds>(now - last_click_);
            const float dx = event.position.x - last_click_pos_.x;
            const float dy = event.position.y - last_click_pos_.y;
            if (click_count_ > 0 && dt.count() < 450 &&
                dx * dx + dy * dy < 16.0f) {
                ++click_count_;
            } else {
                click_count_ = 1;
            }
            last_click_ = now;
            last_click_pos_ = event.position;
            const int cadence = (click_count_ - 1) % 3;
            pending_select_ =
                cadence == 1 ? 1 : cadence == 2 ? 2 : 0;
            dragging_ = true;
            pending_click_ = event.position;
            pending_extend_ = event.is_shift;
            Invalidate();
            return true;
        }
        case Event::Type::MouseMove:
            if (vbar_drag_) {
                ScrollVTo(event.position.y);
                Invalidate();
                return true;
            }
            if (hbar_drag_) {
                ScrollHTo(event.position.x);
                Invalidate();
                return true;
            }
            if (dragging_) {
                pending_click_ = event.position;
                pending_extend_ = true;   // extend while dragging
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (vbar_drag_ || hbar_drag_) {
                if (event.mouse_button == MouseButton::Left) {
                    vbar_drag_ = hbar_drag_ = false;
                    Invalidate();
                    return true;
                }
                return false;
            }
            if (dragging_ && event.mouse_button == MouseButton::Left) {
                dragging_ = false;
                pending_click_ = event.position;
                pending_extend_ = HasSelection();
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseWheel: {
            if (!GetBounds().Contains(event.position)) return false;
            const float max_scroll = MaxScrollY();
            if (event.is_shift) {
                scroll_x_ = std::clamp(
                    scroll_x_ -
                        static_cast<float>(event.wheel_delta) / 120.0f *
                            40.0f,
                    0.0f, MaxScrollX());
            } else {
                scroll_line_ = std::clamp(
                    scroll_line_ -
                        static_cast<float>(event.wheel_delta) / 120.0f *
                            3.0f,
                    0.0f, max_scroll);
            }
            Invalidate();
            return true;
        }
        case Event::Type::KeyDown:
            if (event.is_ctrl && !event.is_alt &&
                HandleShortcut(event.key, event.is_shift)) {
                return true;
            }
            switch (event.key) {
                case KeyCode::Backspace:
                    if (read_only_) return true;
                    PushUndo();
                    Backspace();
                    coalesce_typing_ = false;
                    Changed();
                    return true;
                case KeyCode::Delete:
                    if (read_only_) return true;
                    PushUndo();
                    DeleteForward();
                    coalesce_typing_ = false;
                    Changed();
                    return true;
                case KeyCode::Enter:
                    if (read_only_) return true;
                    PushUndo();
                    InsertNewline();
                    coalesce_typing_ = false;
                    Changed();
                    return true;
                case KeyCode::Tab:
                    if (read_only_) return false;
                    PushUndo();
                    if (HasSelection() || event.is_shift) {
                        IndentLines(event.is_shift);
                    } else {
                        InsertBytes(IndentUnit());
                    }
                    coalesce_typing_ = false;
                    Changed();
                    return true;
                case KeyCode::ArrowLeft:
                    MoveHorizontal(-1, event.is_shift);
                    return true;
                case KeyCode::ArrowRight:
                    MoveHorizontal(1, event.is_shift);
                    return true;
                case KeyCode::ArrowUp:
                    MoveVertical(-1, event.is_shift);
                    return true;
                case KeyCode::ArrowDown:
                    MoveVertical(1, event.is_shift);
                    return true;
                case KeyCode::Home:
                    MoveLineStartEnd(false, event.is_shift);
                    return true;
                case KeyCode::End:
                    MoveLineStartEnd(true, event.is_shift);
                    return true;
                case KeyCode::PageUp:
                    MovePage(-1, event.is_shift);
                    return true;
                case KeyCode::PageDown:
                    MovePage(1, event.is_shift);
                    return true;
                case KeyCode::Escape:
                    SetFocused(false);
                    Invalidate();
                    return true;
                default:
                    return false;
            }
        case Event::Type::TextInput: {
            if (read_only_) return true;
            if (!coalesce_typing_) {
                PushUndo();
                coalesce_typing_ = true;
            }
            if (HasSelection()) {
                coalesce_typing_ = false;
                DeleteSelection();
            }
            // Same walk as Textarea: newlines split, other C0 controls
            // dropped, UTF-8 sequences verbatim. '\t' is skipped — Tab
            // is handled as a KeyDown (indent/dedent), so the stray
            // WM_CHAR must not double-insert. Pair chars get auto-pair
            // semantics via HandlePairInput.
            std::size_t i = 0;
            const std::string& t = event.text;
            while (i < t.size()) {
                const auto c = static_cast<unsigned char>(t[i]);
                if (c == '\r' || c == '\n') {
                    InsertNewline();
                    coalesce_typing_ = false;
                    ++i;
                    if (c == '\r' && i < t.size() && t[i] == '\n') ++i;
                } else if (c < 0x20u) {
                    ++i;
                } else {
                    const std::size_t take =
                        std::min(Utf8SeqLen(c), t.size() - i);
                    if (take == 1 &&
                        HandlePairInput(static_cast<char>(c))) {
                        ++i;
                        continue;
                    }
                    InsertBytes(std::string_view(t).substr(i, take));
                    i += take;
                }
            }
            // Keep a pair-wrap's selection; collapse otherwise.
            if (!HasSelection()) {
                anchor_line_ = caret_line_;
                anchor_col_ = caret_col_;
            }
            Changed();
            return true;
        }
        default:
            return false;
    }
}

// ---- render --------------------------------------------------------------------------

void CodeEditorWidget::Render(XER::UI::Rendering::Renderer& renderer,
                              const XER::UI::Theme::Tokens& tokens) {
    const Rect b = GetBounds();
    renderer.FillRect(b, tokens.input, 0.0f);
    renderer.StrokeRect(b, IsFocused() ? tokens.ring : tokens.border,
                        IsFocused() ? 1.5f : 1.0f, 0.0f);

    pad_ = tokens.spacing_small;
    // Code reads best a touch larger than the UI body font, in a real
    // monospace face (TextStyle::monospace -> Consolas on Windows).
    font_size_ = tokens.font_body + 1.5f;
    line_height_ = font_size_ * 1.45f;

    const Palette pal = MakePalette(tokens);
    const XER::UI::Rendering::TextStyle gutter_style{
        tokens.muted, tokens.font_small, 400, /*monospace=*/true};
    const XER::UI::Rendering::TextStyle body_style{
        tokens.foreground, font_size_, 400, /*monospace=*/true};

    const int digits =
        static_cast<int>(std::to_string(lines_.size()).size());
    const float digit_w =
        std::max(4.0f, renderer.MeasureText("8", gutter_style).width);
    gutter_w_ = 14.0f + static_cast<float>(std::max(3, digits)) *
                            digit_w +
                8.0f;

    if (pending_click_.has_value()) {
        ResolveClick(*pending_click_, pending_extend_, renderer);
        pending_click_.reset();
    }

    // Scrollable content width: measuring just the longest-BYTES line
    // is a cheap stand-in for the widest one.
    constexpr float kBar = 9.0f;
    constexpr float kRail = 8.0f;  // painted bar thickness (ScrollArea)
    if (longest_line_ < lines_.size()) {
        const std::string& longest = lines_[longest_line_];
        content_w_ =
            renderer.MeasureText(longest, body_style).width;
        if (!longest.empty()) {
            avg_char_w_ = content_w_ /
                          static_cast<float>(longest.size());
        }
    } else {
        content_w_ = 0.0f;
    }
    const float text_view_w = b.width - TextX() - pad_;
    hbar_visible_ = content_w_ > text_view_w + 0.5f;
    hbar_h_ = hbar_visible_ ? kBar + 2.0f : 0.0f;
    vbar_visible_ = MaxScrollY() > 0.5f;

    renderer.PushClip(b);

    // Gutter background + separator.
    const Rect gutter{b.x, b.y, TextX() - 4.0f, b.height};
    renderer.FillRect(gutter, tokens.panel, 0.0f);
    renderer.FillRect({b.x + TextX() - 4.0f, b.y, 1.0f, b.height},
                      tokens.border, 0.0f);

    const int first = std::clamp(
        static_cast<int>(std::floor(scroll_line_)), 0,
        static_cast<int>(lines_.size()) - 1);
    const float frac = scroll_line_ - static_cast<float>(first);
    const int vis = VisibleLines() + 2;

    const auto [sel_fl, sel_fc] = SelFrom();
    const auto [sel_tl, sel_tc] = SelTo();

    if (lines_.size() == 1 && lines_[0].empty() &&
        !placeholder_.empty()) {
        renderer.DrawText(
            placeholder_,
            {b.x + TextX(), b.y + pad_, b.width - TextX() - pad_,
             line_height_},
            {tokens.muted, font_size_, 400, /*monospace=*/true});
    }

    for (int i = 0; i < vis; ++i) {
        const int row = first + i;
        if (row >= static_cast<int>(lines_.size())) break;
        const float y =
            b.y + pad_ +
            (static_cast<float>(i) - frac) * line_height_;
        const Rect row_rect{b.x, y, b.width, line_height_};

        // Current-line highlight — the design-system hover wash, kept
        // faint so the token colors stay readable on top.
        if (row == caret_line_ && IsFocused()) {
            XER::UI::Theme::Color line_wash = tokens.hover;
            line_wash.a *= 0.55f;
            renderer.FillRect(row_rect, line_wash, 0.0f);
        }

        // Gutter number.
        const std::string num = std::to_string(row + 1);
        renderer.DrawText(
            num,
            {b.x + 4.0f, y + (line_height_ - tokens.font_small) * 0.5f,
             gutter_w_ - 12.0f, tokens.font_small},
            gutter_style);

        const std::string& line = lines_[static_cast<std::size_t>(row)];
        const float tx = b.x + TextX() - scroll_x_;

        // Selection overlay for this row.
        if (HasSelection() && row >= sel_fl && row <= sel_tl) {
            const int sc = row == sel_fl ? sel_fc : 0;
            const float sx = tx + ColumnWidth(renderer, row, sc);
            // Rows fully inside the selection paint to line end plus a
            // little tail (the "newline is selected too" convention).
            const float ex =
                row == sel_tl
                    ? tx + ColumnWidth(renderer, row, sel_tc)
                    : tx + ColumnWidth(
                              renderer, row,
                              static_cast<int>(line.size())) +
                          line_height_ * 0.4f;
            renderer.FillRect({sx, y, std::max(2.0f, ex - sx),
                               line_height_},
                              tokens.selection, 0.0f);
        }

        // Search-match overlays.
        for (std::size_t mi = 0; mi < matches_.size(); ++mi) {
            const SearchMatch& m = matches_[mi];
            if (m.line != row) continue;
            const float mx = tx + ColumnWidth(renderer, row, m.col);
            const float mw = ColumnWidth(renderer, row, m.col + m.len) -
                             ColumnWidth(renderer, row, m.col);
            const bool act = static_cast<int>(mi) == active_match_;
            renderer.FillRect(
                {mx, y + (act ? 0.0f : line_height_ * 0.5f),
                 std::max(2.0f, mw),
                 act ? line_height_ : line_height_ * 0.5f},
                act ? tokens.accent
                    : XER::UI::Theme::Color{tokens.accent.r,
                                            tokens.accent.g,
                                            tokens.accent.b, 0.35f},
                0.0f);
        }

        // Tokenized text runs — each span's x comes from measuring its
        // prefix with the same monospace style, so per-span placement
        // stays exact under any font change.
        if (row < static_cast<int>(spans_.size())) {
            for (const HighlightSpan& sp :
                 spans_[static_cast<std::size_t>(row)]) {
                const std::string_view piece =
                    std::string_view(line).substr(sp.begin,
                                                  sp.end - sp.begin);
                if (piece.empty()) continue;
                const float x =
                    tx + renderer.MeasureText(
                             std::string_view(line).substr(0, sp.begin),
                             body_style)
                             .width;
                const auto col =
                    pal.colors[static_cast<int>(sp.kind)];
                renderer.DrawText(
                    piece, {x, y + (line_height_ - font_size_) * 0.5f,
                            b.Right() - x - pad_, font_size_},
                    {col, font_size_, 400, /*monospace=*/true});
            }
        } else {
            renderer.DrawText(
                line,
                {tx, y + (line_height_ - font_size_) * 0.5f,
                 b.Right() - tx - pad_, font_size_},
                body_style);
        }
    }

    // Caret.
    if (IsFocused()) {
        EnsureCaretVisible();
        // Exact horizontal visibility — the event-time approximation
        // used avg_char_w_; refine with a real measurement now.
        const float caret_px =
            ColumnWidth(renderer, caret_line_, caret_col_);
        if (caret_px < scroll_x_) {
            scroll_x_ = caret_px;
        } else if (caret_px > scroll_x_ + text_view_w - 10.0f) {
            scroll_x_ = caret_px - text_view_w + 10.0f;
        }
        scroll_x_ =
            std::clamp(scroll_x_, 0.0f, MaxScrollX() + 40.0f);
        const int vis_row = caret_line_ - first;
        if (vis_row >= 0 && vis_row < vis) {
            const float cy =
                b.y + pad_ +
                (static_cast<float>(vis_row) - frac) * line_height_;
            const float cx = b.x + TextX() - scroll_x_ +
                             ColumnWidth(renderer, caret_line_,
                                         caret_col_);
            renderer.FillRect({cx, cy + 1.0f, 2.0f, line_height_ - 2.0f},
                              tokens.accent, 0.0f);
        }
    }
    renderer.PopClip();

    // Thin overlay scrollbars (drawn over the text edges), painted like
    // Widgets::ScrollBarWidget: a faint border pill rail plus a muted
    // pill thumb that brightens while dragged.
    const auto with_alpha = [](XER::UI::Theme::Color c, float a) {
        c.a *= a;
        return c;
    };
    if (vbar_visible_) {
        // The hit strip stays a few px wider than the painted rail.
        vbar_track_ = {b.Right() - kBar - 2.0f, b.y + 1.0f, kBar,
                       b.height - hbar_h_ - 2.0f};
        const Rect rail{vbar_track_.Right() - kRail,
                        vbar_track_.y + 1.0f, kRail,
                        vbar_track_.height - 2.0f};
        const float frac_v = std::clamp(
            static_cast<float>(VisibleLines()) /
                std::max(1.0f, static_cast<float>(lines_.size())),
            0.0f, 1.0f);
        const float th =
            std::max(24.0f, rail.height * frac_v);
        const float max_sc = MaxScrollY();
        const float pos =
            max_sc > 0.0f
                ? (rail.height - th) *
                      std::clamp(scroll_line_ / max_sc, 0.0f, 1.0f)
                : 0.0f;
        vbar_thumb_ = {rail.x, rail.y + pos, rail.width, th};
        renderer.FillRect(rail, with_alpha(tokens.border, 0.22f),
                          rail.width * 0.5f);
        renderer.FillRect(
            vbar_thumb_,
            with_alpha(vbar_drag_ ? tokens.foreground : tokens.muted,
                       vbar_drag_ ? 0.95f : 0.55f),
            vbar_thumb_.width * 0.5f);
    }
    if (hbar_visible_) {
        hbar_track_ = {b.x + TextX(), b.Bottom() - kBar - 2.0f,
                       text_view_w - (vbar_visible_ ? kBar : 0.0f),
                       kBar};
        const Rect rail{hbar_track_.x,
                        hbar_track_.Bottom() - kRail - 1.0f,
                        hbar_track_.width, kRail};
        const float frac_h = std::clamp(
            text_view_w / std::max(1.0f, content_w_ + 2.0f * pad_),
            0.0f, 1.0f);
        const float tw =
            std::max(24.0f, rail.width * frac_h);
        const float max_sc = MaxScrollX();
        const float pos =
            max_sc > 0.0f
                ? (rail.width - tw) *
                      std::clamp(scroll_x_ / max_sc, 0.0f, 1.0f)
                : 0.0f;
        hbar_thumb_ = {rail.x + pos, rail.y, tw, rail.height};
        renderer.FillRect(rail, with_alpha(tokens.border, 0.22f),
                          rail.height * 0.5f);
        renderer.FillRect(
            hbar_thumb_,
            with_alpha(hbar_drag_ ? tokens.foreground : tokens.muted,
                       hbar_drag_ ? 0.95f : 0.55f),
            hbar_thumb_.height * 0.5f);
    }
}

void CodeEditorWidget::Layout(const Rect& bounds) {
    SetBounds(bounds);
}

}  // namespace XER::IdeXejso
