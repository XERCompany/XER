/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "analyzer.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

#include "common.h"

#if XER_SYNC_DEV_HAVE_LIBCLANG
#include <clang-c/Index.h>
#endif

namespace xer_sync_dev {
namespace {

// Directories that never contain build-relevant library sources.
const std::set<std::string>& SkipDirs() {
    static const std::set<std::string> k = {
        ".git", ".svn", ".hg", ".github", ".gitlab",
        "test", "tests", "testing", "unittest", "unittests",
        "example", "examples", "sample", "samples", "demo", "demos",
        "benchmark", "benchmarks", "bench",
        "doc", "docs", "documentation", "doxygen", "man", "po",
        "fuzz", "fuzzer", "fuzzers", "fuzzing", "corpus",
        "ci", "scripts", "tools", "tool", "programs", "apps",
        "contrib", "m4", "visualc", "visual", "cmake",
        "third_party", "vendor", "deps", "external", "extern",
        "node_modules", "__pycache__", "build", "out", "dist",
        "pkgconfig", "pkg-config", "data",
    };
    return k;
}

// Filename-stem tokens that mark a translation unit as non-library
// code (tests, fuzzers, benchmarks, runnable mains).
const std::set<std::string>& SkipFileTokens() {
    static const std::set<std::string> k = {
        "test", "tests", "testing", "unittest", "unittests",
        "fuzz", "fuzzer", "fuzzers", "fuzzing",
        "bench", "benchmark", "benchmarks",
        "example", "sample", "demo",
        // Alternate-implementation markers: upstream build files select
        // exactly one backend (e.g. pthreadpool src/shim.c is the
        // Emscripten backend — upstream never compiles it for native,
        // and it has no preprocessor guard to no-op itself).
        "shim", "mock", "fake",
    };
    return k;
}

// Non-Windows platform markers in path components / file stems.
// Matching is exact-token (split on _ - .), so "unistd" or "pthreads"
// do NOT trigger; "unix", "posix", "linux" directories do.
const std::set<std::string>& NonWinTokens() {
    static const std::set<std::string> k = {
        "linux", "unix", "posix", "mac", "macos", "osx", "darwin",
        "bsd", "freebsd", "openbsd", "netbsd", "dragonfly",
        "android", "ios", "iphoneos", "watchos", "tvos",
        "aix", "solaris", "sunos", "hpux", "haiku", "qnx", "nto",
        "emscripten", "wasi", "wasm", "uwp", "hurd", "plan9", "vms",
        "os2", "amiga", "beos", "cygwin", "vita", "psp", "ps3", "ps4",
        "ps5", "switch", "nacl", "gnu",
    };
    return k;
}

// Compiler/OS macros that describe the *build platform* — reported in
// the platform-guard bucket rather than as config knobs.
const std::set<std::string>& PlatformMacros() {
    static const std::set<std::string> k = {
        "_WIN32", "_WIN64", "WIN32", "WIN64", "_WINDOWS", "_WINNT",
        "_MSC_VER", "_MSC_FULL_VER", "_MSC_BUILD", "_MSVC_LANG",
        "__GNUC__", "__GNUC_MINOR__", "__GNUC_PATCHLEVEL__", "__GNUG__",
        "__clang__", "__clang_major__", "__clang_minor__", "__llvm__",
        "__cplusplus", "__STDC__", "__STDC_VERSION__",
        "__APPLE__", "__MACH__", "__linux__", "__linux", "linux",
        "__unix__", "__unix", "unix", "__FreeBSD__", "__NetBSD__",
        "__OpenBSD__", "__DragonFly__", "__bsdi__",
        "__ANDROID__", "ANDROID", "__EMSCRIPTEN__", "__CYGWIN__",
        "__MINGW32__", "__MINGW64__", "__MINGW64_VERSION_MAJOR",
        "__x86_64__", "_M_X64", "_M_AMD64", "__i386__", "_M_IX86",
        "__arm__", "_M_ARM", "__aarch64__", "_M_ARM64",
        "__ARM_NEON", "__ARM_NEON__", "__SSE__", "__SSE2__", "__SSE3__",
        "__SSSE3__", "__SSE4_1__", "__SSE4_2__", "__AVX__", "__AVX2__",
        "__AVX512F__", "__FMA__", "__F16C__", "__PPC__", "__PPC64__",
        "__BIG_ENDIAN__", "__LITTLE_ENDIAN__", "__BYTE_ORDER__",
        "__ORDER_BIG_ENDIAN__", "__ORDER_LITTLE_ENDIAN__",
        "__wasm__", "__wasm32__", "__wasm64__", "__ELF__",
        "__hpux", "__sun", "__solaris__", "_AIX", "__HAIKU__",
        "__QNX__", "QNXNTO", "__GLIBC__", "__UCLIBC__", "_DEBUG",
        "NDEBUG",
    };
    return k;
}

// Well-known include-prefix -> manifest dep aliases for edge
// classification (e.g. <absl/...> comes from the "abseil-cpp" dep).
const std::map<std::string, std::string>& PrefixAliases() {
    static const std::map<std::string, std::string> k = {
        {"absl", "abseil-cpp"}, {"abseil", "abseil-cpp"},
        {"SDL", "sdl2"}, {"SDL2", "sdl2"},
        {"uv", "libuv"}, {"libuv", "libuv"},
        {"libxml", "libxml2"}, {"libxml2", "libxml2"}, {"xml", "libxml2"},
        {"avcodec", "ffmpeg"}, {"avformat", "ffmpeg"},
        {"avutil", "ffmpeg"}, {"avfilter", "ffmpeg"},
        {"avdevice", "ffmpeg"}, {"swscale", "ffmpeg"},
        {"swresample", "ffmpeg"}, {"postproc", "ffmpeg"},
        {"zlib", "zlib"}, {"zstd", "zstd"}, {"brotli", "brotli"},
        {"re2", "re2"}, {"fp16", "fp16"}, {"fxdiv", "fxdiv"},
        {"pthreadpool", "pthreadpool"}, {"assimp", "assimp"},
        {"dlfcn", "dlfcn-win32"}, {"dl", "dlfcn-win32"},
        {"cef", "cef"}, {"include", ""},
    };
    return k;
}

bool IsHeader(const fs::path& p) {
    const std::string e = ToLower(p.extension().string());
    return e == ".h" || e == ".hpp" || e == ".hh" || e == ".hxx" ||
           e == ".inc" || e == ".inl";
}

bool IsSource(const fs::path& p) {
    const std::string e = ToLower(p.extension().string());
    return e == ".c" || e == ".cc" || e == ".cpp" || e == ".cxx" ||
           e == ".c++" || e == ".m" || e == ".mm";
}

// True when any path component (dir names + file stem) carries a
// non-Windows platform token. `include/` etc. are never tokens.
bool HasNonWinToken(const fs::path& rel) {
    for (const auto& comp : rel) {
        const std::string s = ToLower(comp.stem().string());
        for (const auto& tok : SplitTokens(s, "_-.")) {
            if (NonWinTokens().count(tok)) return true;
        }
    }
    return false;
}

bool StemHasSkipToken(const fs::path& p) {
    const std::string s = ToLower(p.stem().string());
    for (const auto& tok : SplitTokens(s, "_-.")) {
        if (SkipFileTokens().count(tok)) return true;
    }
    return false;
}

// dir is relative to dep root. True when some component is a skip dir.
bool UnderSkipDir(const fs::path& rel) {
    for (const auto& comp : rel) {
        const std::string c = ToLower(comp.string());
        if (SkipDirs().count(c)) return true;
        if (!c.empty() && c[0] == '.') return true;  // hidden dirs
    }
    return false;
}

std::string RelOf(const fs::path& root, const fs::path& p) {
    std::error_code ec;
    const fs::path rel = fs::relative(p, root, ec);
    if (ec) return p.generic_string();
    return rel.generic_string();
}

// C/C++ standard-library spellings. A dep shipping a private
// "string.h" (libxml2/src/string.h!) must not steal <string.h> edges
// from every consumer — classify standard names as system before
// probing dep dirs. POSIX headers (dlfcn.h, pthread.h) are
// deliberately NOT here: they are real dep-provided headers
// (dlfcn-win32 literally ships dlfcn.h).
const std::set<std::string>& StdHeaders() {
    static const std::set<std::string> k = {
        // C library
        "assert.h", "complex.h", "ctype.h", "errno.h", "fenv.h",
        "float.h", "inttypes.h", "iso646.h", "limits.h", "locale.h",
        "math.h", "setjmp.h", "signal.h", "stdalign.h", "stdarg.h",
        "stdatomic.h", "stdbit.h", "stdbool.h", "stddef.h", "stdint.h",
        "stdio.h", "stdlib.h", "stdnoreturn.h", "string.h",
        "strings.h", "tgmath.h", "threads.h", "time.h", "uchar.h",
        "wchar.h", "wctype.h",
        // C++ library (no-extension spellings)
        "algorithm", "any", "array", "atomic", "bit", "bitset",
        "cassert", "cctype", "cerrno", "cfenv", "cfloat", "charconv",
        "chrono", "cinttypes", "climits", "clocale", "cmath",
        "codecvt", "compare", "complex", "concepts",
        "condition_variable", "coroutine", "csetjmp", "csignal",
        "cstdarg", "cstdbool", "cstddef", "cstdint", "cstdio",
        "cstdlib", "cstring", "ctime", "cuchar", "cwchar", "cwctype",
        "deque", "exception", "execution", "filesystem", "format",
        "forward_list", "fstream", "functional", "future",
        "initializer_list", "iomanip", "ios", "iosfwd", "iostream",
        "istream", "iterator", "latch", "limits", "list", "locale",
        "map", "memory", "memory_resource", "mutex", "new",
        "numbers", "numeric", "optional", "ostream", "queue",
        "random", "ranges", "ratio", "regex", "scoped_allocator",
        "semaphore", "set", "shared_mutex", "source_location",
        "span", "sstream", "stack", "stdexcept", "stop_token",
        "streambuf", "string", "string_view", "syncstream",
        "system_error", "thread", "tuple", "type_traits",
        "typeindex", "typeinfo", "unordered_map", "unordered_set",
        "utility", "valarray", "variant", "vector", "version",
    };
    return k;
}

// Extracts add_library()/add_executable() target names from an
// upstream CMakeLists.txt — best-effort regex pass.
std::vector<std::string> UpstreamLibTargets(const fs::path& cmake_file) {
    std::vector<std::string> out;
    std::ifstream in(cmake_file);
    if (!in.is_open()) return out;
    static const std::regex re(
        R"(^\s*add_library\s*\(\s*([A-Za-z0-9_+.\-]+))",
        std::regex::icase);
    std::string line;
    std::smatch m;
    while (std::getline(in, line)) {
        if (line.find('#') != std::string::npos) {
            line = line.substr(0, line.find('#'));
        }
        if (std::regex_search(line, m, re)) {
            const std::string t = m[1].str();
            // Skip obvious imported/alias-only or test helpers — the
            // caller picks the first plausible candidate anyway.
            if (ToLower(t).find("test") == std::string::npos &&
                std::find(out.begin(), out.end(), t) == out.end()) {
                out.push_back(t);
            }
        }
    }
    return out;
}

// --- textual #include / macro-guard scan ------------------------------

struct FileFacts {
    std::vector<std::string> includes;   // spellings in <> or ""
    std::set<std::string> guard_macros;  // names used in #if(n)def/defined()
    std::set<std::string> file_guards;   // include-guard macro of this file
};

FileFacts ScanFileText(const fs::path& file) {
    FileFacts f;
    std::ifstream in(file);
    if (!in.is_open()) return f;

    static const std::regex inc_re(
        R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
    static const std::regex ifdef_re(
        R"(^\s*#\s*(ifdef|ifndef)\s+([A-Za-z_]\w*))");
    static const std::regex if_re(R"(^\s*#\s*(if|elif)\b(.*)$)");
    static const std::regex defined_re(
        R"(defined\s*\(?\s*([A-Za-z_]\w*)\s*\)?)");
    static const std::regex define_re(
        R"(^\s*#\s*define\s+([A-Za-z_]\w*))");

    std::string line;
    std::string pending_ifndef;  // classic include-guard detection
    int guard_window = 0;
    int lines = 0;
    while (std::getline(in, line) && lines++ < 20000) {
        std::smatch m;
        if (std::regex_search(line, m, inc_re)) {
            f.includes.push_back(m[1].str());
            pending_ifndef.clear();
            continue;
        }
        if (std::regex_search(line, m, ifdef_re)) {
            const std::string name = m[2].str();
            f.guard_macros.insert(name);
            if (f.guard_macros.size() + f.includes.size() <= 3 &&
                m[1].str() == "ifndef") {
                pending_ifndef = name;
                guard_window = 4;
            }
            continue;
        }
        if (std::regex_search(line, m, if_re)) {
            const std::string rest = m[2].str();
            for (std::sregex_iterator it(rest.begin(), rest.end(),
                                       defined_re),
                 end;
                 it != end; ++it) {
                f.guard_macros.insert((*it)[1].str());
            }
            pending_ifndef.clear();
            continue;
        }
        if (!pending_ifndef.empty() &&
            std::regex_search(line, m, define_re) &&
            m[1].str() == pending_ifndef) {
            f.file_guards.insert(pending_ifndef);
            pending_ifndef.clear();
        }
        if (guard_window > 0 && --guard_window == 0) {
            pending_ifndef.clear();
        }
    }
    return f;
}

// True when `name` looks like an include guard rather than a config
// knob: FOO_H, FOO_H_, FOO_HPP, FOO_INCLUDED, ...
bool LooksLikeIncludeGuard(const std::string& name) {
    static const char* kSuffixes[] = {
        "_H", "_H_", "_HPP", "_HPP_", "_HH", "_HH_", "_HXX",
        "_INCLUDED", "_INCLUDED_", "_INCL", "_INC", "_INC_",
        "_HEADER", "_HEADER_", "_GUARD", "_GUARD_", "_DEFINED",
    };
    const std::string up = ToUpper(name);
    for (const char* s : kSuffixes) {
        if (EndsWith(up, s)) return true;
    }
    return false;
}

// Resolves a textual include spelling against: dep's own include dirs,
// dep root, then every other dep's include dirs. Returns:
//   "self"    — resolves inside this dep
//   dep name  — resolves inside another manifest dep
//   ""        — unresolved (system header or missing)
std::string ClassifySpelling(
    const std::string& spelling, const fs::path& dep_root,
    const std::vector<std::string>& dep_inc_dirs,
    const std::map<std::string, std::vector<fs::path>>& other_deps,
    std::string* matched_dep) {
    for (const auto& d : dep_inc_dirs) {
        const fs::path cand = dep_root / d / spelling;
        std::error_code ec;
        if (fs::exists(cand, ec)) return "self";
    }
    std::error_code ec;
    if (fs::exists(dep_root / spelling, ec)) return "self";

    // Standard-library spellings are system regardless of whether a
    // dep happens to vendor a file with the same name.
    if (StdHeaders().count(spelling)) return "";

    const std::string first =
        spelling.substr(0, spelling.find('/'));
    auto alias_it = PrefixAliases().find(first);
    if (alias_it != PrefixAliases().end() && !alias_it->second.empty()) {
        *matched_dep = alias_it->second;
        return alias_it->second;
    }

    for (const auto& [name, dirs] : other_deps) {
        for (const auto& d : dirs) {
            if (fs::exists(d / spelling, ec)) {
                *matched_dep = name;
                return name;
            }
        }
        // Convention match: <depname/foo.h> or <DepName.h>
        if (first == name || ToLower(first) == ToLower(name) ||
            ToLower(first) == ToLower(name) + ".h") {
            *matched_dep = name;
            return name;
        }
    }
    return "";
}

#if XER_SYNC_DEV_HAVE_LIBCLANG

struct ClangCtx {
    fs::path dep_root;
    const std::map<std::string, std::vector<fs::path>>* other_deps;
    std::map<std::string, int>* inter_dep;
    std::map<std::string, int>* system_inc;
    int* internal;
};

bool UnderDir(const fs::path& p, const fs::path& dir) {
    std::error_code ec;
    // Canonicalize + case-fold: libclang may return 8.3/lowercase
    // spellings on Windows that fs::relative() then misclassifies.
    std::string ps = fs::weakly_canonical(p, ec).generic_string();
    std::string ds = fs::weakly_canonical(dir, ec).generic_string();
    if (ec) {
        ps = p.generic_string();
        ds = dir.generic_string();
    }
    ps = ToLower(std::move(ps));
    ds = ToLower(std::move(ds));
    if (ps == ds) return true;
    return StartsWith(ps, ds + "/");
}

void InclusionVisitor(CXFile included_file,
                      CXSourceLocation* /*inclusion_stack*/,
                      unsigned /*include_len*/, CXClientData client_data) {
    auto* ctx = static_cast<ClangCtx*>(client_data);
    const CXString cxname = clang_getFileName(included_file);
    const std::string name = clang_getCString(cxname);
    clang_disposeString(cxname);
    if (name.empty()) return;

    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(name, ec);
    const fs::path p = ec ? fs::path(name) : resolved;

    if (UnderDir(p, ctx->dep_root)) {
        ++(*ctx->internal);
        return;
    }
    // A resolved std header is a toolchain header — even when a -I
    // path let it resolve into another dep's src/ (vendored
    // coincidence, e.g. libxml2's private string.h).
    if (StdHeaders().count(ToLower(p.filename().generic_string()))) {
        ++(*ctx->system_inc)[p.filename().generic_string()];
        return;
    }
    for (const auto& [dep, dirs] : *ctx->other_deps) {
        for (const auto& d : dirs) {
            if (UnderDir(p, d)) {
                ++(*ctx->inter_dep)[dep];
                return;
            }
        }
    }
    ++(*ctx->system_inc)[p.filename().generic_string()];
}

// Parses each probed header as a TU and harvests the resolved include
// graph through clang_getInclusions.
void RunLibClang(const fs::path& dep_root,
                 const std::vector<std::string>& probe_headers,
                 const std::vector<std::string>& dep_inc_dirs,
                 const AnalyzeOptions& opts,
                 std::map<std::string, int>* lc_edges,
                 std::map<std::string, int>* lc_system,
                 int* lc_internal, std::string* warn) {
    CXIndex index = clang_createIndex(/*excludePCH=*/0,
                                      /*displayDiagnostics=*/0);
    if (!index) {
        *warn = "libclang: clang_createIndex failed";
        return;
    }

    std::vector<std::string> arg_storage = {
        "-x", "c++", "-std=c++17", "-w", "-ferror-limit=0",
        "-fsyntax-only",
    };
    for (const auto& d : dep_inc_dirs) {
        arg_storage.push_back("-I" + GenericPath(dep_root / d));
    }
    arg_storage.push_back("-I" + GenericPath(dep_root));
    for (const auto& [name, dirs] : opts.include_dirs_of_deps) {
        for (const auto& d : dirs) {
            arg_storage.push_back("-I" + GenericPath(d));
        }
    }
    for (const auto& d : opts.extra_include_dirs) {
        arg_storage.push_back("-I" + d);
    }
    if (!opts.sysroot_include.empty()) {
        arg_storage.push_back("-isystem");
        arg_storage.push_back(opts.sysroot_include);
    }
    std::vector<const char*> args;
    args.reserve(arg_storage.size());
    for (const auto& a : arg_storage) args.push_back(a.c_str());

    ClangCtx ctx{dep_root, &opts.include_dirs_of_deps, lc_edges,
                 lc_system, lc_internal};

    int probed = 0;
    for (const auto& rel : probe_headers) {
        const fs::path header = dep_root / rel;
        CXTranslationUnit tu = clang_parseTranslationUnit(
            index, GenericPath(header).c_str(), args.data(),
            static_cast<int>(args.size()), nullptr, 0,
            CXTranslationUnit_DetailedPreprocessingRecord |
                CXTranslationUnit_SkipFunctionBodies);
        if (tu) {
            ++probed;
            clang_getInclusions(tu, InclusionVisitor, &ctx);
            clang_disposeTranslationUnit(tu);
        }
    }
    clang_disposeIndex(index);
    if (probed == 0) {
        *warn = "libclang: no probe header produced a translation unit";
    }
}
#endif  // XER_SYNC_DEV_HAVE_LIBCLANG

}  // namespace

bool AnalyzeDep(const Dependency& dep, const fs::path& dep_root,
                const AnalyzeOptions& opts, DepAnalysis* out) {
    if (!out) return false;
    *out = DepAnalysis{};
    out->name = dep.name;
    out->root = dep_root;

    std::error_code ec;
    if (!fs::is_directory(dep_root, ec)) {
        out->warning = "dep directory missing: " + dep_root.string();
        return false;
    }

    std::set<std::string> include_dir_set;
    std::vector<std::string> all_headers, all_sources;

    for (fs::recursive_directory_iterator it(
             dep_root, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end && !ec; it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), dep_root, ec);
        if (ec) continue;
        const std::string leaf = ToLower(it->path().filename().string());

        // Upstream build-system detection (any depth for templates,
        // root only for the marker files).
        if (it->is_regular_file(ec)) {
            const bool at_root = rel.parent_path().empty();
            if (at_root && leaf == "cmakelists.txt") {
                out->has_upstream_cmake = true;
            }
            if (at_root && (leaf == "configure" || leaf == "configure.ac" ||
                            leaf == "makefile.am" || leaf == "autogen.sh" ||
                            leaf == "bootstrap")) {
                out->has_autotools = true;
            }
            if (leaf == "meson.build") out->has_meson = true;
            const std::string lname = leaf;
            if (EndsWith(lname, ".in") || EndsWith(lname, ".cmake") ||
                EndsWith(lname, ".am")) {
                out->config_templates.push_back(rel.generic_string());
            }
        }

        if (UnderSkipDir(rel)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }

        if (it->is_directory(ec)) {
            if (leaf == "include" || leaf == "inc" ||
                leaf == "includes" || leaf == "headers" ||
                leaf == "public") {
                include_dir_set.insert(rel.generic_string());
            }
            continue;
        }
        if (!it->is_regular_file(ec)) continue;

        if (IsHeader(it->path())) {
            all_headers.push_back(rel.generic_string());
        } else if (IsSource(it->path())) {
            all_sources.push_back(rel.generic_string());
        }
    }

    if (out->has_upstream_cmake) {
        out->upstream_lib_targets =
            UpstreamLibTargets(dep_root / "CMakeLists.txt");
    }

    // Fallback include dirs when no include/ layout exists.
    if (include_dir_set.empty()) {
        bool root_headers = false, src_headers = false;
        for (const auto& h : all_headers) {
            const fs::path r(h);
            if (r.parent_path().empty()) root_headers = true;
            const std::string top =
                r.begin() != r.end() ? ToLower(r.begin()->string()) : "";
            if (top == "src") src_headers = true;
        }
        if (root_headers) include_dir_set.insert(".");
        if (src_headers) include_dir_set.insert("src");
        if (include_dir_set.empty() && !all_headers.empty()) {
            // Deepest common dir of the first header — last resort.
            include_dir_set.insert(
                fs::path(all_headers.front()).parent_path()
                    .generic_string());
        }
    }
    out->include_dirs.assign(include_dir_set.begin(),
                             include_dir_set.end());

    // Public headers = those under a discovered include dir; fall back
    // to all headers when the layout gave us none.
    for (const auto& h : all_headers) {
        const fs::path parent = fs::path(h).parent_path();
        bool pub = false;
        for (const auto& d : out->include_dirs) {
            const std::string ds = d == "." ? "" : d;
            if (ds.empty() ? parent.empty()
                           : parent.generic_string() == ds ||
                                 StartsWith(parent.generic_string(),
                                            ds + "/")) {
                pub = true;
                break;
            }
        }
        if (pub) out->public_headers.push_back(h);
    }
    if (out->public_headers.empty()) out->public_headers = all_headers;

    // Sources: drop test/fuzz stems and non-Windows platform files.
    for (const auto& s : all_sources) {
        if (StemHasSkipToken(s) || HasNonWinToken(s)) {
            out->excluded_sources.push_back(s);
        } else {
            out->sources.push_back(s);
        }
    }
    out->header_only = out->sources.empty() && !out->public_headers.empty();

    // Self include dirs (absolute) for the other_deps map exclusion.
    std::map<std::string, std::vector<fs::path>> other =
        opts.include_dirs_of_deps;
    other.erase(dep.name);

    // --- textual scan over every file --------------------------------
    std::map<std::string, int> text_edges, text_system;
    int text_internal = 0, text_unresolved = 0;
    int scanned = 0;
    for (const auto& rel :
         [&] {
             std::vector<std::string> all = out->public_headers;
             all.insert(all.end(), out->sources.begin(),
                        out->sources.end());
             return all;
         }()) {
        const fs::path file = dep_root / rel;
        if (fs::file_size(file, ec) > 2u * 1024 * 1024) continue;
        if (++scanned > 1200) break;  // perf bound on monorepo-ish deps

        const FileFacts f = ScanFileText(file);
        for (const auto& inc : f.includes) {
            std::string matched;
            const std::string bucket = ClassifySpelling(
                inc, dep_root, out->include_dirs, other, &matched);
            if (bucket == "self") {
                ++text_internal;
            } else if (!bucket.empty()) {
                ++text_edges[bucket];
            } else {
                ++text_unresolved;
                const std::string first =
                    inc.substr(0, inc.find('/'));
                ++text_system[inc];
            }
        }
        for (const auto& m : f.guard_macros) {
            if (f.file_guards.count(m)) continue;
            if (LooksLikeIncludeGuard(m)) continue;
            if (PlatformMacros().count(m)) {
                ++out->platform_macros[m];
            } else {
                ++out->config_macros[m];
            }
        }
    }
    text_edges.erase(dep.name);  // self-edges are not dependencies
    out->internal_includes = text_internal;
    out->unresolved_includes = text_unresolved;
    out->inter_dep_includes = text_edges;
    out->system_includes = text_system;

    // --- libclang pass (authoritative where it probed) ---------------
#if XER_SYNC_DEV_HAVE_LIBCLANG
    if (opts.use_libclang) {
        std::map<std::string, int> lc_edges, lc_system;
        int lc_internal = 0;
        std::vector<std::string> probe = out->public_headers;
        if (probe.size() > static_cast<size_t>(opts.max_libclang_headers)) {
            probe.resize(static_cast<size_t>(opts.max_libclang_headers));
        }
        // The dep's own include dirs are search paths but never an
        // edge — `other` already has self erased.
        AnalyzeOptions lc_opts = opts;
        lc_opts.include_dirs_of_deps = other;
        std::string warn;
        RunLibClang(dep_root, probe, out->include_dirs, lc_opts,
                    &lc_edges, &lc_system, &lc_internal, &warn);
        if (!warn.empty()) {
            out->warning = warn;
        } else {
            out->libclang_used = true;
            // Merge: libclang counts are authoritative per dep edge —
            // keep whichever pass saw more (textual covers sources too,
            // libclang covers the probe headers with real resolution).
            for (const auto& [d, n] : lc_edges) {
                auto& slot = out->inter_dep_includes[d];
                slot = std::max(slot, n);
            }
            out->internal_includes =
                std::max(out->internal_includes, lc_internal);
        }
    }
#endif

    // A dep that needs a configure step before its headers are usable.
    // (ffmpeg's config.h, zstd's zconf.h is shipped, libxml2's
    // config.h.in, ...) — cmake_gen turns this into a warning block.
    if (out->has_autotools && !out->has_upstream_cmake &&
        out->warning.empty()) {
        out->warning =
            "autotools-only upstream: generated patch cannot fully "
            "configure this dep (needs ./configure + generated headers)";
    }

    return true;
}

}  // namespace xer_sync_dev
