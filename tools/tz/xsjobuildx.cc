// xsjobuildx.cc — XSJOBuildX: compile a C++ library into a
// .xejslib container (see tools/tz/xejslib.h for the format).
//
//   XSJOBuildX <srcdir> -o <lib.xejslib> [src.cc ...] [options]
//
// Scans <srcdir> recursively for *.cc/*.cpp/*.cxx (an explicit
// positional list overrides the scan), compiles each in-process via
// the Clang frontend (xsjo_compile.cc — same pipeline ztzmake uses
// for extension natives), packs the resulting .xsjo modules plus a
// .xejshead header bundle into the XJSL container. Libraries compile
// against the same ve.core.h SDK resolution ztzmake uses, so a lib
// can use the extension ABI exactly like an extension TU.
//
// Consumers: ztzmake --dep lib.xejslib, manifest.toml
// [native] deps = ["libs/lib.xejslib"], or simply drop the file into
// the extension's libs/ dir — modules land as
// mobjective/lib_<stem>.xsjo and headers feed the extension's -I.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "xejslib.h"
#include "xsjo_compile.h"

namespace {

namespace fs = std::filesystem;

void Usage(const char* argv0) {
    std::fprintf(stderr,
        "XSJOBuildX — XER .xejslib library compiler\n"
        "  %s <srcdir> -o <lib.xejslib> [src.cc ...] [options]\n"
        "Options:\n"
        "  -o <path>           output .xejslib (required)\n"
        "  --name <n>          library name (default: srcdir leaf)\n"
        "  --version <v>       library version (default \"1.0\")\n"
        "  --include-root <d>  header root for the .xejshead bundle\n"
        "                      (default: <srcdir>/include when it\n"
        "                      exists, else <srcdir>)\n"
        "  --no-headers        pack no headers\n"
        "  -I <dir>            extra include dir (repeatable)\n"
        "  --sdk <dir>         ve.core.h SDK dir — overrides the\n"
        "                      conventional locations\n"
        "  --clang-prefix <d>  LLVM/Clang toolchain root for the\n"
        "                      resource dir + system headers\n"
        "  -O<0-3>             optimization level (default 2)\n"
        "  -v                  verbose\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string src_dir, out_path, name, version = "1.0";
    std::string include_root, explicit_sdk, clang_prefix;
    std::vector<std::string> extra_includes, explicit_sources;
    bool no_headers = false, verbose = false;
    int opt_level = 2;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "--name" && i + 1 < argc) {
            name = argv[++i];
        } else if (a == "--version" && i + 1 < argc) {
            version = argv[++i];
        } else if (a == "--include-root" && i + 1 < argc) {
            include_root = argv[++i];
        } else if (a == "--no-headers") {
            no_headers = true;
        } else if (a == "-I" && i + 1 < argc) {
            extra_includes.push_back(argv[++i]);
        } else if (a.rfind("-I", 0) == 0 && a.size() > 2) {
            extra_includes.push_back(a.substr(2));
        } else if (a == "--sdk" && i + 1 < argc) {
            explicit_sdk = argv[++i];
        } else if (a == "--clang-prefix" && i + 1 < argc) {
            clang_prefix = argv[++i];
        } else if (a.rfind("-O", 0) == 0 && a.size() == 3 &&
                   a[2] >= '0' && a[2] <= '3') {
            opt_level = a[2] - '0';
        } else if (a == "-v") {
            verbose = true;
        } else if (a == "-h" || a == "--help") {
            Usage(argv[0]);
            return 0;
        } else if (!a.empty() && a.front() != '-' &&
                   src_dir.empty()) {
            src_dir = a;
        } else if (!a.empty() && a.front() != '-') {
            explicit_sources.push_back(a);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n",
                         a.c_str());
            Usage(argv[0]);
            return 2;
        }
    }
    if (src_dir.empty() || out_path.empty()) {
        Usage(argv[0]);
        return 2;
    }

    std::error_code ec;
    const fs::path root(src_dir);
    if (!fs::is_directory(root, ec)) {
        std::fprintf(stderr,
                     "XSJOBuildX: source dir not found: %s\n",
                     src_dir.c_str());
        return 1;
    }
    if (name.empty()) {
        name = root.filename().generic_string();
        if (name.empty()) name = "xejslib";
    }

    // ---- source list: explicit args, else scan srcdir ------------
    std::vector<fs::path> sources;
    if (!explicit_sources.empty()) {
        for (const std::string& s : explicit_sources) {
            fs::path p(s);
            if (p.is_relative()) p = root / p;
            if (!fs::is_regular_file(p, ec)) {
                std::fprintf(stderr,
                             "XSJOBuildX: source missing: %s\n",
                             s.c_str());
                return 1;
            }
            sources.push_back(p);
        }
    } else {
        for (const auto& de :
             fs::recursive_directory_iterator(root, ec)) {
            if (!de.is_regular_file(ec)) continue;
            if (XER::Xsjo::IsNativeSourceName(
                    de.path().filename().string())) {
                sources.push_back(de.path());
            }
        }
        std::sort(sources.begin(), sources.end());
    }
    if (sources.empty()) {
        std::fprintf(stderr,
                     "XSJOBuildX: no C++ sources under %s\n",
                     src_dir.c_str());
        return 1;
    }

    // ---- include root for the header bundle ----------------------
    fs::path inc_root(include_root);
    if (include_root.empty()) {
        inc_root = fs::is_directory(root / "include", ec)
                       ? root / "include"
                       : root;
    }

    // ---- compile every source -> .xsjo ---------------------------
    XER::Xsjo::CompileOptions copts;
    copts.clang_prefix = clang_prefix;
    copts.opt_level = opt_level;
    const std::string exe_dir = XER::Xejs::ExeDir(argv[0]);
    // -I order: srcdir -> include root -> SDK -> caller flags.
    copts.include_dirs.push_back(root.generic_string());
    copts.include_dirs.push_back(inc_root.generic_string());
    for (const std::string& d : XER::Xejs::ResolveSdkDirs(
             exe_dir, root.generic_string(), explicit_sdk)) {
        copts.include_dirs.push_back(d);
    }
    for (const std::string& d : extra_includes) {
        copts.include_dirs.push_back(d);
    }

    XER::Xejs::Lib lib;
    lib.name = name;
    lib.version = version;
    std::set<std::string> stems;
    for (const fs::path& src : sources) {
        XER::Xsjo::CompileOptions src_opts = copts;
        src_opts.include_dirs.insert(
            src_opts.include_dirs.begin(),
            src.parent_path().generic_string());
        std::vector<std::uint8_t> xsjo;
        std::string cerr, warn;
        if (!XER::Xsjo::CompileToXsjo(src.string(), src_opts, &xsjo,
                                      &cerr, &warn)) {
            std::fprintf(stderr, "XSJOBuildX: %s\n", cerr.c_str());
            return 1;
        }
        if (!warn.empty()) {
            std::fprintf(stderr, "%s", warn.c_str());
        }
        XER::Xsjo::XsjoInfo info;
        std::string perr;
        if (!XER::Xsjo::ParseXsjo(xsjo, &info, &perr)) {
            std::fprintf(stderr,
                         "XSJOBuildX: internal error — %s\n",
                         perr.c_str());
            return 1;
        }
        if (lib.triple.empty()) lib.triple = info.triple;
        if (info.triple != lib.triple) {
            std::fprintf(stderr,
                         "XSJOBuildX: triple mismatch on %s\n",
                         src.string().c_str());
            return 1;
        }
        XER::Xejs::Module m;
        m.stem = src.stem().generic_string();
        m.source_name = info.source_name;
        m.xsjo_bytes = std::move(xsjo);
        if (!stems.insert(m.stem).second) {
            std::fprintf(stderr,
                         "XSJOBuildX: duplicate module stem '%s' — "
                         "rename one of the sources\n",
                         m.stem.c_str());
            return 1;
        }
        if (verbose) {
            std::fprintf(stderr,
                         "XSJOBuildX: compiled %s -> module %s "
                         "(%llu bytes)\n",
                         src.string().c_str(), m.stem.c_str(),
                         static_cast<unsigned long long>(
                             m.xsjo_bytes.size()));
        }
        lib.modules.push_back(std::move(m));
    }

    // ---- pack headers ---------------------------------------------
    if (!no_headers) {
        std::string herr;
        if (!XER::Xejs::CollectHeaders(inc_root.string(),
                                       &lib.headers, &herr)) {
            std::fprintf(stderr, "XSJOBuildX: %s\n", herr.c_str());
            return 1;
        }
        if (verbose) {
            for (const auto& h : lib.headers) {
                std::fprintf(stderr, "XSJOBuildX: header %s "
                                     "(%llu bytes)\n",
                             h.name.c_str(),
                             static_cast<unsigned long long>(
                                 h.bytes.size()));
            }
        }
    }

    std::string werr;
    if (!XER::Xejs::WriteLibFile(lib, out_path, &werr)) {
        std::fprintf(stderr, "XSJOBuildX: %s\n", werr.c_str());
        return 1;
    }
    std::printf("XSJOBuildX: %s -> %s (%zu modules, %zu headers)\n",
                src_dir.c_str(), out_path.c_str(),
                lib.modules.size(), lib.headers.size());
    return 0;
}
