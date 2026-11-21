// ide_format.cc — see ide_format.h.
#include "ide_format.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#if defined(XER_IDE_HAS_CLANG)
#include "clang/Format/Format.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/Error.h"
#endif

namespace XER::IdeXejso {
namespace {

std::string LowerExt(const std::string& rel_path) {
    const auto dot = rel_path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = rel_path.substr(dot + 1);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(
            static_cast<unsigned char>(c)));
    }
    return ext;
}

#if defined(XER_IDE_HAS_CLANG)
// Walks up from the file's directory looking for a clang-format config.
bool ClangFormatConfigExists(const std::string& file_name) {
    namespace fs = std::filesystem;
    if (file_name.empty()) return false;
    fs::path dir = fs::path(file_name).parent_path();
    std::error_code ec;
    for (int up = 0; up < 10 && !dir.empty(); ++up) {
        if (fs::exists(dir / ".clang-format", ec) ||
            fs::exists(dir / "_clang-format", ec)) {
            return true;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return false;
}
#endif

// ---- builtin JS beautifier ----------------------------------------------------
//
// Strategy: lex to a token stream (whitespace is implicit — every
// token records the newlines/spaces that preceded it), then re-emit
// with normalized indentation and single-space-or-nothing decisions
// between tokens. Tokens are never created, removed, merged or
// reordered, and lines are never joined/split, so formatting can only
// change whitespace — the same transformation applied twice is a
// fixed point.

enum class JsTok {
    Ident,     // identifiers AND keywords (keywords checked separately)
    Number,
    String,    // '...' "..." `...` — verbatim, may contain newlines
    Regex,     // /.../flags — verbatim
    Comment,   // //... or /*...*/ — verbatim, may contain newlines
    Punct,     // operator or punctuation
};

struct JsToken {
    JsTok kind;
    std::string text;
    int newlines_before = 0;   // '\n' count in the ws run before it
    bool space_before = false; // ws run had ws but no '\n'
};

bool JsIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_' || c == '$' || c >= 0x80;
}
bool JsIdentChar(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '$' || c >= 0x80;
}
bool JsDigit(unsigned char c) { return std::isdigit(c); }

// The full JS keyword list isn't needed — the beautifier only asks
// the two questions below (space before '('? regex allowed after?).

// Words that keep a space before a following '(': "if (", "return (",
// "function (" — everything else with a word before '(' is a call.
bool JsSpaceBeforeParen(std::string_view word) {
    static const std::unordered_set<std::string_view> s = {
        "if", "for", "while", "switch", "catch", "with", "function",
        "return", "throw", "typeof", "delete", "void", "in", "of",
        "new", "instanceof", "case", "yield", "await", "else", "do",
        "import",
    };
    return s.count(word) != 0;
}

// A '/' after one of these keywords can open a regex literal.
bool JsKeywordAllowsRegex(std::string_view w) {
    static const std::unordered_set<std::string_view> s = {
        "return", "typeof", "case", "in", "of", "new", "delete",
        "void", "throw", "else", "do", "yield", "await", "instanceof",
    };
    return s.count(w) != 0;
}

// Punct tokens that can END an operand — a '/' after them is division.
bool JsOperandCloser(std::string_view p) {
    return p == ")" || p == "]" || p == "++" || p == "--";
}

std::size_t JsScanQuoted(std::string_view s, std::size_t i, char q) {
    ++i;
    while (i < s.size()) {
        if (s[i] == '\\') { i += 2; continue; }
        if (s[i] == q) return i + 1;
        ++i;
    }
    return s.size();
}

// Backtick template literal — may span newlines; ${...} stays inside
// the token (verbatim by contract).
std::size_t JsScanTemplate(std::string_view s, std::size_t i) {
    ++i;
    while (i < s.size()) {
        if (s[i] == '\\') { i += 2; continue; }
        if (s[i] == '`') return i + 1;
        ++i;
    }
    return s.size();
}

std::size_t JsScanRegex(std::string_view s, std::size_t i) {
    ++i;
    bool in_class = false;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\\') { i += 2; continue; }
        if (c == '[') { in_class = true; ++i; continue; }
        if (c == ']') { in_class = false; ++i; continue; }
        if (c == '/' && !in_class) { ++i; break; }
        if (c == '\n') break;    // unterminated — give up
        ++i;
    }
    while (i < s.size() &&
           JsIdentChar(static_cast<unsigned char>(s[i]))) ++i;
    return i;
}

std::string_view JsPunctAt(std::string_view s, std::size_t i) {
    static const char* k3[] = {"===", "!==", "**=", "<<=", ">>=",
                               ">>>", "&&=", "||=", "?\?=", "..."};
    static const char* k2[] = {"=>", "==", "!=", "<=", ">=",
                               "&&", "||", "??", "++", "--", "+=",
                               "-=", "*=", "/=", "%=", "**", "<<",
                               ">>", "&=", "|=", "^="};
    const std::string_view rest = s.substr(i);
    if (rest.size() >= 4 && rest.substr(0, 4) == ">>>=") {
        return rest.substr(0, 4);
    }
    for (const char* p : k3) {
        if (rest.size() >= 3 && rest.substr(0, 3) == p) {
            return rest.substr(0, 3);
        }
    }
    // "?." is optional chaining only before a member/call/index —
    // "a ? .5 : b" must keep its '?' as a ternary token.
    if (rest.size() >= 2 && rest.substr(0, 2) == "?.") {
        if (rest.size() > 2) {
            const unsigned char n =
                static_cast<unsigned char>(rest[2]);
            if (JsIdentStart(n) || n == '(' || n == '[') {
                return rest.substr(0, 2);
            }
        }
        return rest.substr(0, 1);
    }
    for (const char* p : k2) {
        if (rest.size() >= 2 && rest.substr(0, 2) == p) {
            return rest.substr(0, 2);
        }
    }
    return rest.substr(0, 1);
}

std::vector<JsToken> JsLex(const std::string& code) {
    std::vector<JsToken> out;
    out.reserve(code.size() / 4 + 8);
    std::size_t i = 0;
    int nl = 0;
    bool sp = false;
    const auto push = [&](JsTok kind, std::string text) {
        out.push_back({kind, std::move(text), nl, sp});
        nl = 0;
        sp = false;
    };
    while (i < code.size()) {
        const char c = code[i];
        if (c == '\r') { ++i; continue; }
        if (c == '\n') { ++nl; sp = false; ++i; continue; }
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            sp = true; ++i; continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') {
            const auto e = code.find('\n', i);
            const std::size_t end =
                e == std::string::npos ? code.size() : e;
            push(JsTok::Comment, code.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '*') {
            const auto e = code.find("*/", i + 2);
            const std::size_t end = e == std::string::npos
                                        ? code.size() : e + 2;
            push(JsTok::Comment, code.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '\'' || c == '"') {
            const std::size_t end = JsScanQuoted(code, i, c);
            push(JsTok::String, code.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '`') {
            const std::size_t end = JsScanTemplate(code, i);
            push(JsTok::String, code.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '/' && i + 1 < code.size()) {
            // Regex iff the previous significant token cannot end an
            // operand (division otherwise).
            bool regex = false;
            if (out.empty()) {
                regex = true;
            } else {
                const JsToken& p = out.back();
                if (p.kind == JsTok::Punct) {
                    regex = !JsOperandCloser(p.text);
                } else if (p.kind == JsTok::Ident) {
                    regex = JsKeywordAllowsRegex(p.text);
                } else if (p.kind == JsTok::Comment) {
                    regex = true;
                }
            }
            if (regex) {
                const std::size_t end = JsScanRegex(code, i);
                push(JsTok::Regex, code.substr(i, end - i));
                i = end;
                continue;
            }
            // else: division — falls into the punct branch below
        }
        if (JsDigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < code.size() &&
             JsDigit(static_cast<unsigned char>(code[i + 1])))) {
            const std::size_t b = i;
            while (i < code.size()) {
                const unsigned char d =
                    static_cast<unsigned char>(code[i]);
                if (std::isalnum(d) || d == '.' || d == '_') {
                    ++i;
                } else if ((d == '+' || d == '-') && i > b &&
                           (code[i - 1] == 'e' || code[i - 1] == 'E' ||
                            code[i - 1] == 'p' || code[i - 1] == 'P')) {
                    ++i;
                } else {
                    break;
                }
            }
            push(JsTok::Number, code.substr(b, i - b));
            continue;
        }
        if (JsIdentStart(static_cast<unsigned char>(c))) {
            const std::size_t b = i;
            while (i < code.size() &&
                   JsIdentChar(static_cast<unsigned char>(code[i]))) {
                ++i;
            }
            push(JsTok::Ident, code.substr(b, i - b));
            continue;
        }
        const std::string_view p = JsPunctAt(code, i);
        push(JsTok::Punct, std::string(p));
        i += p.size();
    }
    return out;
}

bool JsWordish(const JsToken& t) {
    return t.kind == JsTok::Ident || t.kind == JsTok::Number ||
           t.kind == JsTok::String || t.kind == JsTok::Regex;
}

// True when `cur` sits in prefix position (unary-capable): after an
// opening bracket/operator/separator, at statement start, or after a
// prefix-friendly keyword.
bool JsPrefixPosition(const JsToken* prev, const JsToken* preprev) {
    (void)preprev;
    if (prev == nullptr) return true;
    if (prev->kind == JsTok::Punct) {
        return !JsOperandCloser(prev->text) && prev->text != "}" &&
               prev->text != ")" && prev->text != "]";
    }
    if (prev->kind == JsTok::Ident) {
        return JsKeywordAllowsRegex(prev->text);
    }
    return prev->kind == JsTok::Comment;
}

// Whitespace decision between two adjacent same-line tokens.
// Conservative: only the pairs below collapse to zero width —
// everything else gets a single space. Crucially, we never collapse
// between two operator tokens (e.g. `-` `-` must not fuse into `--`)
// or two wordish tokens (`return x`).
bool JsNeedsSpace(const JsToken& prev, const JsToken& cur) {
    const bool pw = JsWordish(prev);
    const bool cw = JsWordish(cur);
    if (pw && cw) return true;
    if (prev.kind == JsTok::Comment || cur.kind == JsTok::Comment) {
        return true;
    }
    const std::string& p = prev.text;
    const std::string& c = cur.text;

    if (c == ")" || c == "]" || c == ";" || c == "," || c == "}" ||
        c == ".") {
        return false;
    }
    if (p == "(" || p == "[" || p == "." || p == "?.") return false;
    if (c == "?." ) return false;
    if (c == "(") {
        if (prev.kind == JsTok::Ident) {
            return JsSpaceBeforeParen(p);
        }
        if (prev.kind == JsTok::Number || prev.kind == JsTok::String ||
            prev.kind == JsTok::Regex) {
            return false;   // call: f( — rare after literal, still none
        }
        if (p == ")" || p == "]") return false;   // (a)(b) / f()[0](x)
        return p != "{" && p != ";" && p != ",";
    }
    if (c == "[") {
        // Indexing when the left side is an operand; array literal
        // after operators/separators gets a space ("= [").
        return !(prev.kind == JsTok::Ident ||
                 prev.kind == JsTok::Number ||
                 prev.kind == JsTok::String ||
                 prev.kind == JsTok::Regex || p == ")" || p == "]");
    }
    if (c == "{") {
        // ") {" / "= {" / word "{" / ": {" get a space; after an
        // opening bracket/separator it hugs ("({", "[{", ",{").
        return !(p == "(" || p == "[" || p == "{" || p == "?" ||
                 p == ",");
    }
    if (c == ":") return false;              // "a: b" — space handled
    if (p == ":") return true;               // after ':' always space
    if (c == "?") return true;               // ternary gets air
    if (p == "?") return true;

    // '}' / ';' / ',' on the left: hug the next closer/separator,
    // space everything else ("} else", "};", "f(a,b)" via ',' after).
    if (p == "}") {
        return c != ";" && c != "," && c != ")" && c != "]" &&
               c != "." && c != ":";
    }
    if (p == ";" || p == ",") return true;

    // Prefix/postfix ++ -- and friends.
    if (c == "++" || c == "--") {
        // Postfix after an operand → hug; prefix → operand spacing.
        const bool postfix = pw || p == ")" || p == "]";
        return !postfix;
    }
    if (p == "++" || p == "--") {
        return !cw;   // postfix: "x++ +y" keeps space before ops only
    }
    if (c == "!" || c == "~" || c == "...") return false;  // prefix hugs
    if (p == "!" || p == "~" || p == "..." ) return false;

    // Unary +/-: in prefix position it hugs its operand and the
    // opening bracket ("(-a"), keeps a gap elsewhere ("return -x").
    if (c == "+" || c == "-") {
        if (JsPrefixPosition(&prev, nullptr)) {
            return !(p == "(" || p == "[");
        }
        return true;    // binary: spaced
    }
    if ((p == "+" || p == "-") &&
        JsPrefixPosition(&prev, nullptr)) {
        // After a prefix +/-: hug operands, but keep the gap before
        // another sign so "- -x" can never fuse into "--x".
        return c == "+" || c == "-" || c == "++" || c == "--";
    }

    // Binary operators get space on both sides; a word next to an
    // operator is spaced too ("a = b", "} = x" impossible but safe).
    return true;
}

std::string JsBeautify(const std::string& code) {
    const std::vector<JsToken> toks = JsLex(code);
    std::string out;
    out.reserve(code.size() + 16);

    int depth = 0;
    bool at_line_start = true;
    const std::string indent_unit = "    ";  // 4 spaces

    for (std::size_t ti = 0; ti < toks.size(); ++ti) {
        const JsToken& t = toks[ti];
        if (t.newlines_before > 0) {
            // Preserve the newline count (blank lines verbatim) but
            // drop trailing whitespace.
            for (int n = 0; n < t.newlines_before; ++n) {
                out.push_back('\n');
            }
            at_line_start = true;
        }
        if (at_line_start) {
            int dedent = 0;
            if (t.kind == JsTok::Punct &&
                (t.text == "}" || t.text == ")" || t.text == "]")) {
                dedent = 1;
            } else if (t.kind == JsTok::Ident &&
                       (t.text == "case" || t.text == "default")) {
                dedent = 1;
            }
            const int ind = std::max(0, depth - dedent);
            for (int n = 0; n < ind; ++n) out += indent_unit;
            at_line_start = false;
        } else if (ti > 0 && t.newlines_before == 0) {
            const JsToken& prev = toks[ti - 1];
            if (JsNeedsSpace(prev, t)) out.push_back(' ');
        }

        out += t.text;

        // Multi-line tokens (block comments, template literals): the
        // text after their last internal newline starts a fresh
        // output line.
        if (t.text.find('\n') != std::string::npos) {
            at_line_start = false;   // we are mid-line after the token
            // Tokens following the comment on its last line behave
            // as if they followed it directly — the flag stays false.
        }

        // Depth accounting — braces inside strings/comments/regex are
        // invisible because those are single tokens.
        if (t.kind == JsTok::Punct) {
            if (t.text == "{" || t.text == "(" || t.text == "[") {
                ++depth;
            } else if (t.text == "}" || t.text == ")" ||
                       t.text == "]") {
                depth = std::max(0, depth - 1);
            }
        }
    }
    // Trailing newline: emit exactly one iff the source's tail had
    // one — the check on the raw text is idempotent.
    if (!out.empty()) {
        const auto tail = code.find_last_not_of(" \t\r");
        if (tail != std::string::npos && tail + 1 < code.size() &&
            code.find('\n', tail) != std::string::npos) {
            while (!out.empty() && out.back() == '\n') {
                out.pop_back();
            }
            out.push_back('\n');
        }
    }
    return out;
}

}  // namespace

// ---- clang-format -------------------------------------------------------------

bool ClangFormatAvailable() {
#if defined(XER_IDE_HAS_CLANG)
    return true;
#else
    return false;
#endif
}

bool FormatCpp(const std::string& code, const std::string& file_name,
               std::string* out, std::string* error) {
#if !defined(XER_IDE_HAS_CLANG)
    (void)code; (void)file_name;
    if (error != nullptr) {
        *error = "clang-format unavailable — IDEXEJSO was built "
                 "without libclang-cpp";
    }
    return false;
#else
    namespace cs = clang;
    cs::format::FormatStyle style;
    if (ClangFormatConfigExists(file_name)) {
        auto styled = cs::format::getStyle(
            "file", file_name, "LLVM", code);
        if (!styled) {
            if (error != nullptr) {
                *error = ".clang-format error: " +
                         llvm::toString(styled.takeError());
            }
            return false;
        }
        style = *styled;
    } else {
        style = cs::format::getLLVMStyle();
        style.IndentWidth = 4;
        style.ColumnLimit = 100;
    }
    const std::vector<cs::tooling::Range> ranges = {
        cs::tooling::Range(0, static_cast<unsigned>(code.size()))};
    cs::format::FormattingAttemptStatus status{};
    const cs::tooling::Replacements reps =
        cs::format::reformat(style, code, ranges, file_name, &status);
    auto applied = cs::tooling::applyAllReplacements(code, reps);
    if (!applied) {
        if (error != nullptr) {
            *error = "clang-format: " +
                     llvm::toString(applied.takeError());
        }
        return false;
    }
    if (!status.FormatComplete && error != nullptr) {
        // Non-fatal: partially formatted output is still returned,
        // but the caller deserves the note in the log.
        *error = "clang-format: incomplete — possible syntax error "
                 "near line " + std::to_string(status.Line);
    }
    *out = std::move(*applied);
    return true;
#endif
}

std::string FormatJs(const std::string& code) {
    return JsBeautify(code);
}

bool FormatSource(const std::string& rel_path, const std::string& code,
                  std::string* out, std::string* engine,
                  std::string* error) {
    const std::string ext = LowerExt(rel_path);
    if (ext == "cc" || ext == "cpp" || ext == "cxx" || ext == "c" ||
        ext == "h" || ext == "hh" || ext == "hpp" || ext == "hxx" ||
        ext == "json") {
        if (engine != nullptr) *engine = "clang-format";
        return FormatCpp(code, rel_path, out, error);
    }
    if (ext == "js" || ext == "mjs" || ext == "cjs") {
        if (engine != nullptr) *engine = "builtin js";
        *out = FormatJs(code);
        return true;
    }
    if (error != nullptr) {
        *error = "no formatter for ." + ext;
    }
    return false;
}

}  // namespace XER::IdeXejso
