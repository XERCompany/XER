// ztzmake.cc — packer for the XER TZ container.
//
//   ztzmake <input_dir> -o <output.tz|.xexrj> [-I<dir>] [--sdk <dir>]
//
// Packs every file under input_dir (recursively) into a TZ archive.
// A fresh OVERFLOW integrity manifest is generated automatically —
// members are stored uncompressed and ordered by name so output is
// deterministic. Pair with ztzvertfy.exe to validate the result.
//
// Native JIT payloads: C++ sources selected for compilation are turned
// into COFF objects IN-PROCESS (Clang frontend, no subprocess) and
// packed as mobjective/<stem>.xsjo instead of the raw source.
// Selection: manifest.toml [native] sources=[...] when present, else
// every *.cc/*.cpp/*.cxx under the input dir's native/ subtree.
// [native] keep_sources = true additionally packs the raw sources.
// The compile needs the extension SDK on the include path — resolved
// from --sdk, $XER_VE_SDK, then a few conventional locations
// (<exe>/sdk, <exe>/assets/sdk, <input>/sdk, assets/sdk siblings).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "tz/tz.h"
#include "xsjo_compile.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

void Usage(const char* argv0) {
    std::fprintf(stderr,
        "ztzmake — XER TZ packer\n"
        "  %s <input_dir> -o <output.tz|.xexrj>\n"
        "Options:\n"
        "  -o <path>     output archive path (required)\n"
        "  -v            list every packed member\n"
        "  -I <dir>      extra include dir for native sources"
        " (repeatable)\n"
        "  --sdk <dir>   extension SDK dir (ve.core.h) — overrides the\n"
        "                conventional locations\n"
        "  --clang-prefix <dir>  LLVM/Clang toolchain root used for the\n"
        "                resource dir + system headers\n",
        argv0);
}

// ---- minimal manifest probe ------------------------------------------
// ztzmake only needs:
//   [native] sources = ["native/a.cc", ...]   — explicit source pick
//   [pack]   keep_sources = true              — also pack raw sources
//   [pack]   sources = [...]                  — alias for the above
// [native] values must stay arrays: the extension host's manifest
// parser rejects non-array values in that section. [pack] is ignored
// by the host entirely, so packer-only options live there.
// Unknown sections/keys are skipped (the full manifest contract is
// enforced later by the extension host's parser).

struct NativeManifest {
    bool has_sources = false;
    std::vector<std::string> sources;
    bool keep_sources = false;
};

std::string TomlUnquote(const std::string& s, bool* ok) {
    *ok = s.size() >= 2 && s.front() == '"' && s.back() == '"';
    if (!*ok) return {};
    std::string out;
    out.reserve(s.size());
    for (size_t i = 1; i + 1 < s.size(); ++i) {
        const char c = s[i];
        if (c == '\\' && i + 2 < s.size()) {
            const char nch = s[i + 1];
            if (nch == '"' || nch == '\\') { out.push_back(nch); ++i; continue; }
            if (nch == 'n') { out.push_back('\n'); ++i; continue; }
            if (nch == 't') { out.push_back('\t'); ++i; continue; }
        }
        out.push_back(c);
    }
    return out;
}

bool TomlParseArray(const std::string& s, std::vector<std::string>* out) {
    std::string t = s;
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
    t = t.substr(1, t.size() - 2);
    out->clear();
    std::string tok;
    bool q = false;
    auto flush = [&]() -> bool {
        const auto b = tok.find_first_not_of(" \t");
        const auto e = tok.find_last_not_of(" \t");
        if (b == std::string::npos) { tok.clear(); return true; }
        bool ok = false;
        auto v = TomlUnquote(tok.substr(b, e - b + 1), &ok);
        tok.clear();
        if (!ok) return false;
        out->push_back(std::move(v));
        return true;
    };
    for (const char c : t) {
        if (c == '"') q = !q;
        if (c == ',' && !q) {
            if (!flush()) return false;
            continue;
        }
        tok.push_back(c);
    }
    return flush();
}

bool ParseNativeManifest(const std::string& text, NativeManifest* out,
                         std::string* error) {
    std::string section;
    size_t lineno = 0;
    size_t pos = 0;
    auto fail = [&](const std::string& msg) {
        if (error) {
            *error = "manifest.toml:" + std::to_string(lineno) +
                     ": " + msg;
        }
        return false;
    };
    while (pos <= text.size()) {
        const auto nl = text.find('\n', pos);
        std::string line =
            text.substr(pos, nl == std::string::npos
                               ? std::string::npos
                               : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        ++lineno;
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const auto b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        const auto e = line.find_last_not_of(" \t\r");
        const std::string s = line.substr(b, e - b + 1);
        if (s.front() == '[' && s.back() == ']') {
            section = s.substr(1, s.size() - 2);
            continue;
        }
        const auto eq = s.find('=');
        if (eq == std::string::npos) {
            return fail("expected key = value");
        }
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        const auto kb = key.find_first_not_of(" \t");
        const auto ke = key.find_last_not_of(" \t");
        const auto vb = val.find_first_not_of(" \t");
        const auto ve = val.find_last_not_of(" \t");
        key = key.substr(kb, ke - kb + 1);
        val = val.substr(vb, ve - vb + 1);
        if (section == "native" && key == "sources") {
            out->has_sources = true;
            if (!TomlParseArray(val, &out->sources)) {
                return fail("expected [\"a.cc\", ...] for sources");
            }
        } else if (section == "pack") {
            if (key == "keep_sources") {
                out->keep_sources = (val == "true");
            } else if (key == "sources") {
                out->has_sources = true;
                if (!TomlParseArray(val, &out->sources)) {
                    return fail("expected [\"a.cc\", ...] for sources");
                }
            }
        }
        // dlls/lua/py and unknown keys are the host's concern.
    }
    return true;
}

// ---- file collection ---------------------------------------------------

std::string RelMemberName(const fs::path& root, const fs::path& p) {
    std::error_code ec;
    const fs::path rel = fs::relative(p, root, ec);
    if (ec) return {};
    std::string s = rel.generic_string();
    return XER::Tz::TzNormalizeName(s);
}

// SDK include-dir candidates, in priority order. Only directories that
// actually exist are returned.
std::vector<std::string> ResolveSdkDirs(const fs::path& exe_dir,
                                        const fs::path& input_dir,
                                        const std::string& explicit_sdk) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto add = [&](const fs::path& p) {
        std::error_code ec;
        if (!fs::is_directory(p, ec)) return;
        const auto key = fs::weakly_canonical(p, ec).string();
        if (!key.empty() && seen.insert(key).second) {
            out.push_back(key);
        }
    };
    if (!explicit_sdk.empty()) add(explicit_sdk);
    if (const char* env = std::getenv("XER_VE_SDK");
        env != nullptr && *env != '\0') {
        add(env);
    }
    add(exe_dir / "sdk");
    add(exe_dir / "assets" / "sdk");
    add(exe_dir.parent_path() / "sdk");
    // Dev-layout fallback: <build>/bin/../.. is the repo root.
    add(exe_dir / ".." / ".." / ".." / "tools" / "video_editor" / "sdk");
    add(input_dir / "sdk");
    add(input_dir / ".." / "sdk");               // extensions_src/sdk
    add(input_dir / ".." / ".." / "sdk");        // assets/sdk
    // tools/video_editor/assets/extensions_src/<ext>/../../../sdk
    // -> tools/video_editor/sdk — the in-tree SDK location.
    add(input_dir / ".." / ".." / ".." / "sdk");
    return out;
}

fs::path ExeDir(const char* argv0) {
#ifdef _WIN32
    // argv[0] can be a bare name resolved via PATH — ask the loader
    // where this exe actually lives.
    wchar_t buf[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, buf, MAX_PATH) != 0) {
        return fs::path(buf).parent_path();
    }
#endif
    std::error_code ec;
    const fs::path exe = fs::canonical(
        fs::path(argv0).has_parent_path()
            ? fs::absolute(argv0, ec)
            : fs::path(argv0),
        ec);
    if (!ec && !exe.empty()) return exe.parent_path();
    return fs::current_path(ec);
}

}  // namespace

int main(int argc, char** argv) {
    std::string input_dir, out_path, explicit_sdk, clang_prefix;
    std::vector<std::string> extra_includes;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "-I" && i + 1 < argc) {
            extra_includes.push_back(argv[++i]);
        } else if (a.rfind("-I", 0) == 0 && a.size() > 2) {
            extra_includes.push_back(a.substr(2));
        } else if (a == "--sdk" && i + 1 < argc) {
            explicit_sdk = argv[++i];
        } else if (a == "--clang-prefix" && i + 1 < argc) {
            clang_prefix = argv[++i];
        } else if (a == "-v") {
            verbose = true;
        } else if (a == "-h" || a == "--help") {
            Usage(argv[0]);
            return 0;
        } else if (!a.empty() && a.front() != '-' &&
                   input_dir.empty()) {
            input_dir = a;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            Usage(argv[0]);
            return 2;
        }
    }
    if (input_dir.empty() || out_path.empty()) {
        Usage(argv[0]);
        return 2;
    }

    std::error_code ec;
    const fs::path root = fs::path(input_dir);
    if (!fs::is_directory(root, ec)) {
        std::fprintf(stderr, "ztzmake: input dir not found: %s\n",
                     input_dir.c_str());
        return 1;
    }
    fs::create_directories(fs::path(out_path).parent_path(), ec);

    // ---- manifest: which sources compile, and are raw sources kept --
    NativeManifest native;
    const fs::path manifest_path = root / "manifest.toml";
    if (fs::is_regular_file(manifest_path, ec)) {
        std::ifstream in(manifest_path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        std::string perr;
        if (!ParseNativeManifest(text, &native, &perr)) {
            std::fprintf(stderr, "ztzmake: %s\n", perr.c_str());
            return 1;
        }
    }

    // Selected native sources, as normalized member names.
    std::set<std::string> selected;
    if (native.has_sources) {
        for (const std::string& s : native.sources) {
            const std::string n = XER::Tz::TzNormalizeName(s);
            if (n.empty()) {
                std::fprintf(stderr,
                             "ztzmake: unsafe [native] source name: %s\n",
                             s.c_str());
                return 1;
            }
            if (!XER::Xsjo::IsNativeSourceName(n)) {
                std::fprintf(stderr,
                             "ztzmake: [native] source not a C++ file: "
                             "%s\n", s.c_str());
                return 1;
            }
            if (!fs::is_regular_file(root / n, ec)) {
                std::fprintf(stderr,
                             "ztzmake: [native] source missing: %s\n",
                             s.c_str());
                return 1;
            }
            selected.insert(n);
        }
    } else {
        // Auto-scan: every native source under <input>/native/.
        const fs::path ndir = root / "native";
        if (fs::is_directory(ndir, ec)) {
            for (const auto& de :
                 fs::recursive_directory_iterator(ndir, ec)) {
                if (!de.is_regular_file(ec)) continue;
                const std::string rel = RelMemberName(root, de.path());
                if (XER::Xsjo::IsNativeSourceName(rel)) {
                    selected.insert(rel);
                }
            }
        }
    }

    // ---- gather members -------------------------------------------------
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::vector<std::pair<std::string, fs::path>> to_compile;
    for (const auto& de : fs::recursive_directory_iterator(root, ec)) {
        if (!de.is_regular_file(ec)) continue;
        const std::string name = RelMemberName(root, de.path());
        if (name.empty()) continue;
        if (name == "OVERFLOW") continue;   // regenerated by TzWrite
        if (selected.count(name)) {
            to_compile.emplace_back(name, de.path());
            if (!native.keep_sources) continue;
        }
        std::ifstream in(de.path(), std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "ztzmake: cannot read %s\n",
                         de.path().string().c_str());
            return 1;
        }
        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        files[name] = std::move(bytes);
    }

    // ---- compile native sources -> mobjective/<stem>.xsjo -------------
    if (!to_compile.empty()) {
        const fs::path exe_dir = ExeDir(argv[0]);
        XER::Xsjo::CompileOptions copts;
        copts.clang_prefix = clang_prefix;
        // -I order: source dirs (via per-file dir below) -> SDK ->
        // caller flags.
        for (const std::string& d :
             ResolveSdkDirs(exe_dir, root, explicit_sdk)) {
            copts.include_dirs.push_back(d);
        }
        for (const std::string& d : extra_includes) {
            copts.include_dirs.push_back(d);
        }
        for (const auto& [member, path] : to_compile) {
            XER::Xsjo::CompileOptions src_opts = copts;
            src_opts.include_dirs.insert(src_opts.include_dirs.begin(),
                                         root.generic_string());
            src_opts.include_dirs.insert(
                src_opts.include_dirs.begin(),
                path.parent_path().generic_string());
            std::vector<std::uint8_t> xsjo;
            std::string cerr;
            if (!XER::Xsjo::CompileToXsjo(path.string(), src_opts,
                                          &xsjo, &cerr)) {
                std::fprintf(stderr, "ztzmake: %s\n", cerr.c_str());
                return 1;
            }
            const std::string stem = path.stem().string();
            const std::string member_name =
                std::string(XER::Xsjo::kArchiveDir) + "/" + stem +
                ".xsjo";
            if (files.count(member_name)) {
                std::fprintf(stderr,
                             "ztzmake: native stem collision on %s\n",
                             member_name.c_str());
                return 1;
            }
            files[member_name] = std::move(xsjo);
            std::fprintf(stderr,
                         "ztzmake: compiled %s -> %s (%llu bytes)\n",
                         member.c_str(), member_name.c_str(),
                         static_cast<unsigned long long>(
                             files[member_name].size()));
        }
    }

    std::string error;
    if (!XER::Tz::TzWrite(files, out_path, &error)) {
        std::fprintf(stderr, "ztzmake: %s\n", error.c_str());
        return 1;
    }

    // Verify what was just written — a packer that produces archives it
    // cannot re-open is worse than no packer.
    const auto report = XER::Tz::TzVerify(out_path);
    if (!report.ok) {
        std::fprintf(stderr, "ztzmake: self-verify failed:\n%s",
                     XER::Tz::TzReportText(report).c_str());
        return 1;
    }
    if (verbose) {
        std::string err2;
        const auto arc = XER::Tz::TzArchive::Open(out_path, &err2);
        if (arc) {
            for (const auto& e : arc->Entries()) {
                std::printf("%10llu  %s\n",
                            static_cast<unsigned long long>(e.size),
                            e.name.c_str());
            }
        }
    }
    std::printf("ztzmake: %s -> %s (%zu entries, %llu bytes)\n",
                input_dir.c_str(), out_path.c_str(), report.entries,
                static_cast<unsigned long long>(report.total_bytes));
    return 0;
}
