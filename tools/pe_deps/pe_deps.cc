// xer_pe_deps — compute the PE import + delay-import DLL closure of a
// Windows binary against a search directory, and optionally copy the
// resolved toolchain DLLs next to the binary.
//
//   xer_pe_deps <binary> <search_dir>
//               [--transitive] [--copy <dst>] [--format list]
//
// For every DLL name found in the PE import directory (data dir 1) and
// the delay-import directory (data dir 13), resolution order is:
//   1. the directory containing <binary>  — sibling YA.*.dll modules
//      and already-staged runtime DLLs are followed transitively but
//      never re-copied;
//   2. <search_dir>                        — the toolchain bin dir; hits
//      are reported and (with --copy) staged into <dst>.
// Names that resolve in neither (KERNEL32.dll, api-ms-*, msvcrt.dll, …)
// are OS-provided and skipped. --transitive repeats the scan on every
// resolved DLL (BFS over a case-insensitive visited set).
//
// Exit codes: 0 success, 1 unreadable/invalid input PE, 2 bad args,
// 3 a --copy operation failed after retries.
//
// The tool links the static LLVM object libraries (LLVMObject /
// LLVMBinaryFormat / LLVMSupport) plus a static libc++/mingw runtime,
// so it runs during the build before any DLL staging exists.

#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/WithColor.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

namespace fs = std::filesystem;
using llvm::object::COFFObjectFile;

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string ErrToString(llvm::Error e) {
    return llvm::toString(std::move(e));
}

// Append the imported + delay-imported DLL names of one PE file to out.
// Returns false (with err set) when the file cannot be read or is not
// a COFF/PE image.
bool CollectImports(const fs::path &path, std::vector<std::string> &out,
                    std::string &err) {
    auto buf_or = llvm::MemoryBuffer::getFile(path.string());
    if (!buf_or) {
        err = path.string() + ": " + buf_or.getError().message();
        return false;
    }
    // Parse as COFF directly (not via createBinary) so the link only
    // needs the COFF object code — pulling every Binary format would
    // drag the IR reader/MC/archive writers into this tiny tool.
    auto coff_or = COFFObjectFile::createCOFFObjectFile(
        (*buf_or)->getMemBufferRef());
    if (!coff_or) {
        err = path.string() + ": " + ErrToString(coff_or.takeError());
        return false;
    }
    const std::unique_ptr<COFFObjectFile> coff = std::move(*coff_or);
    for (const auto &dir : coff->import_directories()) {
        llvm::StringRef name;
        if (llvm::Error e = dir.getName(name)) {
            llvm::consumeError(std::move(e));
            continue;
        }
        out.emplace_back(name.str());
    }
    for (const auto &dir : coff->delay_import_directories()) {
        llvm::StringRef name;
        if (llvm::Error e = dir.getName(name)) {
            llvm::consumeError(std::move(e));
            continue;
        }
        out.emplace_back(name.str());
    }
    return true;
}

// Where a DLL name resolved.
enum class Origin { ExeDir, SearchDir };

struct Resolved {
    fs::path path;
    Origin origin;
};

// Look up a bare DLL name first beside the scanned binary, then in the
// search dir. filename() strips any directory part the import table may
// carry (import names are bare in practice; stay defensive).
bool ResolveDll(const std::string &name, const fs::path &exe_dir,
                const fs::path &search_dir, Resolved &out) {
    const fs::path fname = fs::path(name).filename();
    fs::path cand = exe_dir / fname;
    std::error_code ec;
    if (fs::exists(cand, ec)) {
        out = {cand, Origin::ExeDir};
        return true;
    }
    cand = search_dir / fname;
    if (fs::exists(cand, ec)) {
        out = {cand, Origin::SearchDir};
        return true;
    }
    return false;
}

bool CopyWithRetry(const fs::path &src, const fs::path &dst,
                   std::string &err) {
    std::error_code ec;
    for (int attempt = 0; attempt < 5; ++attempt) {
        ec.clear();
        if (fs::copy_file(src, dst, fs::copy_options::overwrite_existing,
                          ec))
            return true;
        // Dest is often locked briefly by a concurrently-writing
        // sibling POST_BUILD step or an exiting process — back off.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    err = src.string() + " -> " + dst.string() + ": " + ec.message();
    return false;
}

void Usage(const char *argv0) {
    std::cerr
        << "usage: " << argv0
        << " <binary> <search_dir> [--transitive] [--copy <dst>]"
           " [--format list]\n"
           "  prints the sorted unique DLL names that <binary> (and,"
           " with\n"
           "  --transitive, its resolved DLLs) imports from PE import"
           " and\n"
           "  delay-import tables; resolves each name in the binary's"
           " own\n"
           "  directory first, then <search_dir>. --copy stages the"
           " files\n"
           "  that resolved inside <search_dir> into <dst>.\n";
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        Usage(argv[0]);
        return 2;
    }

    fs::path binary = argv[1];
    fs::path search_dir = argv[2];
    bool transitive = false;
    fs::path copy_dst;
    bool do_copy = false;

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--transitive") {
            transitive = true;
        } else if (arg == "--copy") {
            if (i + 1 >= argc) {
                std::cerr << "xer_pe_deps: --copy requires <dst>\n";
                return 2;
            }
            copy_dst = argv[++i];
            do_copy = true;
        } else if (arg == "--format") {
            if (i + 1 >= argc || std::string(argv[i + 1]) != "list") {
                std::cerr << "xer_pe_deps: only --format list is "
                             "supported\n";
                return 2;
            }
            ++i;
        } else {
            std::cerr << "xer_pe_deps: unknown argument '" << arg
                      << "'\n";
            Usage(argv[0]);
            return 2;
        }
    }

    std::error_code ec;
    if (!fs::exists(binary, ec)) {
        std::cerr << "xer_pe_deps: cannot read '" << binary.string()
                  << "'\n";
        return 1;
    }
    const fs::path exe_dir = binary.parent_path();

    // BFS over resolved DLLs. visited + resolved are keyed by the
    // lowercase DLL name so LIBFOO.dll/libfoo.dll collapse.
    std::set<std::string> visited;
    // name -> resolved path for DLLs found in search_dir (copy set).
    std::map<std::string, fs::path> search_hits;
    // every resolved name (exe dir + search dir) — the printed set.
    std::set<std::string> resolved_names;

    std::deque<fs::path> worklist;
    worklist.push_back(binary);
    visited.insert(ToLower(binary.filename().string()));

    std::string err;
    bool first = true;
    while (!worklist.empty()) {
        const fs::path cur = worklist.front();
        worklist.pop_front();

        std::vector<std::string> imports;
        if (!CollectImports(cur, imports, err)) {
            // The root binary must parse; a resolved sibling that fails
            // mid-walk is reported but non-fatal.
            if (first) {
                std::cerr << "xer_pe_deps: " << err << "\n";
                return 1;
            }
            llvm::WithColor::warning()
                << "xer_pe_deps: skipping " << err << "\n";
            continue;
        }
        first = false;

        for (const std::string &name : imports) {
            const std::string key = ToLower(fs::path(name).filename().string());
            if (key.empty() || visited.count(key))
                continue;
            visited.insert(key);

            Resolved r;
            if (!ResolveDll(name, exe_dir, search_dir, r))
                continue;  // OS-provided / not shipped — nothing to do.

            resolved_names.insert(r.path.filename().string());
            if (r.origin == Origin::SearchDir)
                search_hits.emplace(r.path.filename().string(), r.path);
            if (transitive)
                worklist.push_back(r.path);
        }
    }

    for (const std::string &name : resolved_names)
        std::cout << name << "\n";

    if (do_copy) {
        fs::create_directories(copy_dst, ec);
        int copied = 0;
        for (const auto &[name, src] : search_hits) {
            const fs::path dst = copy_dst / src.filename();
            if (!CopyWithRetry(src, dst, err)) {
                std::cerr << "xer_pe_deps: copy failed: " << err << "\n";
                return 3;
            }
            ++copied;
        }
        std::cerr << "xer_pe_deps: " << copied
                  << " DLL(s) staged into " << copy_dst.string() << "\n";
    }
    return 0;
}
