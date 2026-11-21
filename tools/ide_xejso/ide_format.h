// ide_format.h — in-process code formatting for IDEXEJSO.
//
// Two engines, no third-party processes (the hard constraint):
//   C++ (.cc/.h/...) + JSON → clang::format::reformat() from
//                           libclang-cpp, linked into this binary
//                           (XER_IDE_HAS_CLANG — same monolithic
//                           clang-cpp/LLVM link ztzmake uses).
//   JavaScript            → a conservative built-in beautifier written
//                           here: re-indents by bracket depth and
//                           normalizes whitespace BETWEEN tokens only
//                           (strings/comments/regex verbatim, lines
//                           never joined or split). Idempotent by
//                           construction: the token stream is unchanged
//                           by formatting, so a second pass emits the
//                           same whitespace decisions.
// Other languages report "no formatter" — a no-op, never silent.
#pragma once

#include <string>

namespace XER::IdeXejso {

// True when this build links libclang-cpp (XER_IDE_HAS_CLANG).
bool ClangFormatAvailable();

// clang::format::reformat on `code`. `file_name` steers language
// detection and .clang-format lookup (a .clang-format/_clang-format in
// an ancestor dir wins; otherwise LLVM style + IndentWidth 4 +
// ColumnLimit 100). false + `error` on failure — including when Clang
// is not compiled in.
bool FormatCpp(const std::string& code, const std::string& file_name,
               std::string* out, std::string* error);

// The builtin JS beautifier — always succeeds, returns the formatted
// text (see the file header for the contract).
std::string FormatJs(const std::string& code);

// Dispatch by `rel_path` extension:
//   .cc/.cpp/.cxx/.c/.h/.hh/.hpp/.hxx/.json → FormatCpp (clang-format)
//   .js/.mjs/.cjs                            → FormatJs (builtin)
// `engine` receives "clang-format" / "builtin js" for the status line.
// false + `error` when there is no formatter for the extension or the
// engine fails.
bool FormatSource(const std::string& rel_path, const std::string& code,
                  std::string* out, std::string* engine,
                  std::string* error);

}  // namespace XER::IdeXejso
