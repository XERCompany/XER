// xsjo_compile.cc — see xsjo_compile.h for the format + contract.
//
// The compiler runs fully in-process through the Clang frontend
// (libclang-cpp): no clang.exe subprocess, no temp .o — EmitObjAction
// writes the COFF straight into a SmallVector, which is then wrapped
// in the XSJ1 header. Diagnostics are captured to a string through a
// TextDiagnosticPrinter so ztzmake can report a proper error.
#include "xsjo_compile.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <mutex>

#if defined(XER_ZTZ_HAS_CLANG)
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#endif

namespace XER::Xsjo {
namespace {

void PutU16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xff));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xff));
}
void PutU32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (unsigned s = 0; s < 32; s += 8) {
        v.push_back(static_cast<std::uint8_t>((x >> s) & 0xff));
    }
}
void PutU64(std::vector<std::uint8_t>& v, std::uint64_t x) {
    for (unsigned s = 0; s < 64; s += 8) {
        v.push_back(static_cast<std::uint8_t>((x >> s) & 0xff));
    }
}

std::uint16_t GetU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t GetU32(const std::uint8_t* p) {
    std::uint32_t x = 0;
    for (unsigned s = 0; s < 32; s += 8) {
        x |= static_cast<std::uint32_t>(p[s / 8]) << s;
    }
    return x;
}
std::uint64_t GetU64(const std::uint8_t* p) {
    std::uint64_t x = 0;
    for (unsigned s = 0; s < 64; s += 8) {
        x |= static_cast<std::uint64_t>(p[s / 8]) << s;
    }
    return x;
}

}  // namespace

bool IsNativeSourceName(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".cc" || ext == ".cpp" || ext == ".cxx";
}

bool ParseXsjo(const std::vector<std::uint8_t>& bytes, XsjoInfo* info,
               std::string* err) {
    const std::uint8_t* p = bytes.data();
    const std::size_t n = bytes.size();
    auto fail = [&](const char* msg) {
        if (err) *err = msg;
        return false;
    };
    if (n < 4 || GetU32(p) != kMagic) {
        return fail("bad XSJ1 magic");
    }
    if (n < 6) return fail("truncated XSJ1 header");
    if (GetU16(p + 4) != kVersion) {
        return fail("unsupported XSJ1 version");
    }
    std::size_t at = 6;
    auto read_str = [&](std::string* out) {
        if (at + 2 > n) return false;
        const std::uint16_t len = GetU16(p + at);
        at += 2;
        if (at + len > n) return false;
        out->assign(reinterpret_cast<const char*>(p + at), len);
        at += len;
        return true;
    };
    if (!read_str(&info->triple) || !read_str(&info->source_name)) {
        return fail("truncated XSJ1 strings");
    }
    if (at + 8 > n) return fail("truncated XSJ1 payload size");
    const std::uint64_t payload_size = GetU64(p + at);
    at += 8;
    if (payload_size > n - at) {
        return fail("XSJ1 payload overruns the member");
    }
    info->payload = p + at;
    info->payload_size = static_cast<std::size_t>(payload_size);
    return true;
}

#if defined(XER_ZTZ_HAS_CLANG)
namespace {

void InitLlvm() {
    static std::once_flag once;
    std::call_once(once, [] {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
    });
}

// The toolchain prefix that owns the resource dir + libc++ headers.
// Order: explicit --clang-prefix style option > env > compiled-in
// XER_XSJO_CLANG_PREFIX (CMake bakes the build toolchain root in).
std::string ResolveClangPrefix(const CompileOptions& opts) {
    if (!opts.clang_prefix.empty()) return opts.clang_prefix;
    if (const char* env = std::getenv("XER_CLANG_PREFIX");
        env != nullptr && *env != '\0') {
        return env;
    }
#ifdef XER_XSJO_CLANG_PREFIX
    return XER_XSJO_CLANG_PREFIX;
#else
    return {};
#endif
}

// <prefix>/lib/clang/<N>/include — the builtins dir (stddef.h,
// immintrin.h…). Scan for the highest numeric version dir so the same
// binary keeps working across toolchain upgrades; fall back to the
// compile-time LLVM major when the scan finds nothing.
std::string FindResourceDir(const std::string& prefix) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::path(prefix) / "lib" / "clang";
    std::string best;
    long best_ver = -1;
    for (const auto& de : fs::directory_iterator(base, ec)) {
        if (!de.is_directory(ec)) continue;
        const std::string leaf = de.path().filename().string();
        if (!fs::is_directory(de.path() / "include", ec)) continue;
        char* end = nullptr;
        const long v = std::strtol(leaf.c_str(), &end, 10);
        const bool numeric = end != leaf.c_str() && *end == '\0';
        if (numeric ? v > best_ver : best.empty()) {
            best_ver = numeric ? v : best_ver;
            best = de.path().string();
        }
    }
    if (!best.empty()) return best;
#ifdef LLVM_VERSION_MAJOR
    const fs::path guess =
        base / std::to_string(LLVM_VERSION_MAJOR);
    if (fs::is_directory(guess / "include", ec)) return guess.string();
#endif
    return {};
}

}  // namespace

bool CompileToXsjo(const std::string& src_path,
                   const CompileOptions& opts,
                   std::vector<std::uint8_t>* out, std::string* err,
                   std::string* warnings) {
    InitLlvm();
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(src_path, ec)) {
        if (err) *err = "source not found: " + src_path;
        return false;
    }

    // Diagnostics — a TextDiagnosticPrinter renders errors/warnings
    // into diag_text the same way clang prints them.
    clang::DiagnosticOptions diag_opts;
    std::string diag_text;
    llvm::raw_string_ostream diag_os(diag_text);
    auto* client = new clang::TextDiagnosticPrinter(diag_os, diag_opts);
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diag_ids(
        new clang::DiagnosticIDs());
    llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diags(
        new clang::DiagnosticsEngine(diag_ids, diag_opts, client,
                                     /*ShouldOwnClient=*/true));
    // Same default the driver applies: warnings inside system headers
    // stay silent unless -Wsystem-headers is passed.
    diags->setSuppressSystemWarnings(true);

    const std::string prefix = ResolveClangPrefix(opts);
    const std::string resource_dir = FindResourceDir(prefix);

    // CompilerInvocation::CreateFromArgs parses CC1 args — NOT driver
    // args — so the flag set is assembled the way the MinGW clang++
    // driver spells it for an object compile (captured via `clang++ -###`
    // on this toolchain): SEH exceptions, __GNUC__ emulation via
    // -fgnuc-version (without it the MinGW CRT headers cannot parse),
    // pic model + addr-sig like the toolchain default.
    const std::string triple =
        !opts.triple.empty() ? opts.triple
#ifdef LLVM_DEFAULT_TARGET_TRIPLE
                             : LLVM_DEFAULT_TARGET_TRIPLE;
#else
                             : "x86_64-w64-windows-gnu";
#endif
    std::vector<std::string> args{
        "-triple", triple,
        "-emit-obj",
        "-disable-free", "-clear-ast-before-backend",
        "-disable-llvm-verifier", "-discard-value-names",
        "-main-file-name", fs::path(src_path).filename().string(),
        "-mrelocation-model", "pic", "-pic-level", "2",
        "-mframe-pointer=none",
        "-fmath-errno", "-ffp-contract=on", "-fno-rounding-math",
        "-mconstructor-aliases", "-funwind-tables=2",
        "-fno-sized-deallocation", "-fno-use-init-array",
        "-target-cpu", "x86-64", "-tune-cpu", "generic",
        "-std=c++23", "-fdeprecated-macro",
        "-fno-use-cxa-atexit",
        "-fgnuc-version=4.2.1",
        "-fno-implicit-modules", "-fskip-odr-check-in-gmf",
        "-fcxx-exceptions", "-fexceptions", "-exception-model=seh",
        "-vectorize-loops", "-vectorize-slp", "-faddrsig",
        "-O" + std::to_string(opts.opt_level),
        "-o", "xsjo-out",   // setOutputStream() overrides the sink
        "-x", "c++",
        src_path,
    };
    if (!resource_dir.empty()) {
        args.push_back("-resource-dir");
        args.push_back(resource_dir);
    }
    // System include search list, mirroring the driver's MinGW layout —
    // entries that do not exist on a given install are skipped.
    auto add_isystem = [&](const fs::path& d) {
        if (fs::is_directory(d, ec)) {
            args.push_back("-internal-isystem");
            args.push_back(d.generic_string());
        }
    };
    if (!prefix.empty()) {
        const fs::path pre(prefix);
        add_isystem(pre / "x86_64-w64-mingw32" / "include" / "c++" / "v1");
        add_isystem(pre / "include" / "c++" / "v1");
        add_isystem(pre / "x86_64-w64-mingw32" / "include");
        add_isystem(pre / "x86_64-w64-mingw32" / "usr" / "include");
        add_isystem(pre / "include");
    }
    for (const std::string& inc : opts.include_dirs) {
        args.push_back("-I");
        args.push_back(inc);
    }
    for (const std::string& f : opts.extra_flags) {
        args.push_back(f);
    }
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const std::string& a : args) argv.push_back(a.c_str());

    auto invocation = std::make_shared<clang::CompilerInvocation>();
    if (!clang::CompilerInvocation::CreateFromArgs(*invocation, argv,
                                                 *diags)) {
        diag_os.flush();
        if (err) {
            *err = "compiler invocation rejected: " + src_path + "\n" +
                   diag_text;
        }
        return false;
    }

    clang::CompilerInstance ci(std::move(invocation));
    ci.setDiagnostics(diags);

    // EmitObjAction -> COFF object straight into a SmallVector (no
    // temp file, no subprocess).
    llvm::SmallVector<char, 0> obj_buf;
    ci.setOutputStream(
        std::make_unique<llvm::raw_svector_ostream>(obj_buf));

    clang::EmitObjAction act;
    const bool ran = ci.ExecuteAction(act);
    diag_os.flush();
    if (warnings != nullptr) *warnings = diag_text;
    if (!ran || diags->getClient()->getNumErrors() > 0) {
        if (err) {
            *err = "compile failed: " + src_path;
            if (!diag_text.empty()) *err += "\n" + diag_text;
        }
        return false;
    }
    if (obj_buf.empty()) {
        if (err) *err = "compile produced empty object: " + src_path;
        return false;
    }

    // Wrap in the XSJ1 header.
    const std::string name = fs::path(src_path).filename().string();
    out->clear();
    PutU32(*out, kMagic);
    PutU16(*out, kVersion);
    PutU16(*out, static_cast<std::uint16_t>(triple.size()));
    out->insert(out->end(), triple.begin(), triple.end());
    PutU16(*out, static_cast<std::uint16_t>(name.size()));
    out->insert(out->end(), name.begin(), name.end());
    PutU64(*out, static_cast<std::uint64_t>(obj_buf.size()));
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(
        obj_buf.data());
    out->insert(out->end(), bytes, bytes + obj_buf.size());
    return true;
}

#else   // !XER_ZTZ_HAS_CLANG

bool CompileToXsjo(const std::string& src_path, const CompileOptions&,
                   std::vector<std::uint8_t>*, std::string* err,
                   std::string*) {
    if (err) {
        *err = "native compilation not supported: ztzmake was built "
               "without Clang (XER_ZTZ_HAS_CLANG unset) — cannot pack " +
               src_path;
    }
    return false;
}

#endif  // XER_ZTZ_HAS_CLANG

}  // namespace XER::Xsjo
