// xsjo_compile.h — .xsjo "XER source-to-JIT object" packager.
//
// A .xsjo member packs one JIT-compilable translation unit inside a
// .xexrj extension archive (under mobjective/). The payload is a raw
// MinGW COFF object produced IN-PROCESS by the Clang frontend
// (clang::CompilerInstance + EmitObjAction — no clang.exe subprocess),
// wrapped in a small XSJ1 header so a loader can sanity-check the
// target triple before handing the object to the JIT.
//
// Layout (all integers little-endian):
//   [4]   magic "XSJ1"
//   [u16] header_version (=1)
//   [u16] triple_len   [triple bytes]    — e.g. "x86_64-w64-windows-gnu"
//   [u16] name_len     [name bytes]      — original source filename
//   [u64] payload_size
//   [payload_size bytes] COFF object
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Xsjo {

// Member prefix inside the .xexrj archive — mobjective/<stem>.xsjo.
inline constexpr const char* kArchiveDir = "mobjective";
inline constexpr std::uint32_t kMagic = 0x314A5358;   // "XSJ1" LE
inline constexpr std::uint16_t kVersion = 1;

struct CompileOptions {
    // Extra -I dirs (SDK dir, source dir, --sdk / -I CLI flags).
    std::vector<std::string> include_dirs;
    // Raw extra driver flags appended verbatim.
    std::vector<std::string> extra_flags;
    // Empty -> host toolchain triple (MSYS2 clang64:
    // "x86_64-w64-windows-gnu"). Must match what the loader's JIT
    // expects — MinGW COFF, not MSVC.
    std::string triple;
    // Clang toolchain root used to locate the resource dir
    // (lib/clang/<ver>/include) and the libc++/CRT system headers.
    // Empty -> the compiled-in XER_XSJO_CLANG_PREFIX, then the
    // XER_CLANG_PREFIX env var.
    std::string clang_prefix;
    int opt_level = 2;
};

// Compiles src_path (.cc/.cpp/.cxx) into a COFF object and wraps it in
// the XSJ1 container. out receives the complete member bytes.
// False + err (compiler diagnostics included) on failure — including
// when ztzmake was built without Clang (XER_ZTZ_HAS_CLANG unset).
// When non-null, warnings receives rendered compiler diagnostics even
// on success.
bool CompileToXsjo(const std::string& src_path,
                   const CompileOptions& opts,
                   std::vector<std::uint8_t>* out, std::string* err,
                   std::string* warnings = nullptr);

struct XsjoInfo {
    std::string triple;
    std::string source_name;
    const std::uint8_t* payload = nullptr;   // view into the input buffer
    std::size_t payload_size = 0;
};

// Validates + unpacks the XSJ1 header. info->payload points into
// `bytes` — keep the vector alive while using it. No Clang needed.
bool ParseXsjo(const std::vector<std::uint8_t>& bytes, XsjoInfo* info,
               std::string* err);

// True when src has a native-source extension (.cc/.cpp/.cxx).
bool IsNativeSourceName(const std::string& name);

}  // namespace XER::Xsjo
