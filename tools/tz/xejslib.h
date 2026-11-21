// xejslib.h — .xejslib "XER JIT library" container.
//
// A .xejslib is the shippable unit produced by XSJOBuildX.exe: a
// library's compiled C++ sources (each an in-process-Clang COFF
// object wrapped in the XSJ1 container — the SAME bytes a .xexrj
// carries as mobjective/*.xsjo members) plus a header bundle the
// consumer's sources #include against.
//
// Consumption (ztzmake --dep / [native] deps / libs/*.xejslib):
// every embedded module lands in the consuming extension's archive
// as mobjective/lib_<stem>.xsjo; the JIT loader links all of an
// extension's modules into one symbol-resolution mesh (see
// xsjo_jit.h::LinkModules), so extension code calls library symbols
// exactly like a static link. The .xejshead bundle is unpacked to a
// scratch include dir fed to -I while the extension compiles.
//
// .xejslib layout (all integers little-endian):
//   [4]   magic "XJSL"
//   [u16] format_version (=1)
//   [u16] flags (reserved, 0)
//   [u64] json_len   [json bytes]   — manifest header (see below)
//   [u64] head_len   [head bytes]   — XJSH blob, may be 0
//   [u64] module_count
//   module_count times:
//     [u64] size [size bytes]       — complete .xsjo (XSJ1 + COFF)
//
// The JSON header is informational (tooling, --info dumps):
//   {"name":"libdemo","version":"1.0",
//    "triple":"x86_64-w64-windows-gnu",
//    "modules":[{"stem":"dsp","source":"dsp.cc","size":1234}, ...],
//    "headers":{"count":N,"entries":["libdemo/dsp.h", ...]}}
// Consumers do NOT parse it for correctness — module stems are read
// back out of each XSJ1 payload (ParseXsjo source_name), so the
// embedded objects are the single source of truth.
//
// .xejshead blob layout (magic "XJSH" — a standalone flat header
// pack, also usable on its own):
//   [4]   magic "XJSH"
//   [u16] version (=1)
//   [u16] reserved (0)
//   [u32] entry_count
//   per entry: [u16 name_len][name bytes][u64 size][bytes]
// Names are '/'-separated paths relative to the library's include
// root ("libdemo/dsp.h"), so a consumer that puts the unpack dir on
// -I gets #include "libdemo/dsp.h" for free. The first entry is the
// synthesized index.json: {"include_root": "...", "headers": [...]}.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Xejs {

inline constexpr std::uint32_t kLibMagic = 0x4C534A58;    // "XJSL" LE
inline constexpr std::uint32_t kHeadMagic = 0x48534A58;   // "XJSH" LE
inline constexpr std::uint16_t kVersion = 1;

// One file inside the .xejshead bundle.
struct HeaderEntry {
    std::string name;                    // '/'-separated, root-rel
    std::vector<std::uint8_t> bytes;
};

// One compiled module: stem + the complete .xsjo member bytes.
struct Module {
    std::string stem;                    // mobjective/lib_<stem>.xsjo
    std::string source_name;             // original source filename
    std::vector<std::uint8_t> xsjo_bytes;
};

struct Lib {
    std::string name;
    std::string version;
    std::string triple;
    std::vector<Module> modules;
    std::vector<HeaderEntry> headers;
    std::string json;                    // raw JSON header (kept verbatim)
};

// ---- writing --------------------------------------------------------------

// Serializes `lib` to the XJSL container at out_path. Module entries
// are written in `lib.modules` order; the JSON header is (re)built
// from the struct fields (lib.json is ignored on write).
bool WriteLibFile(const Lib& lib, const std::string& out_path,
                  std::string* err);

// ---- reading ---------------------------------------------------------------

// Parses + validates a .xejslib. Each embedded module is unwrapped
// through Xsjo::ParseXsjo — a malformed XSJ1 payload fails the whole
// read, so a lib that passed ReadLibFile is safe to splice into a
// .xexrj. lib->json holds the raw header; name/version/triple are
// best-effort extracted from it (never fatal when absent).
bool ReadLibFile(const std::string& path, Lib* lib, std::string* err);

// ---- .xejshead bundle -------------------------------------------------------

// Serializes header entries into the XJSH blob, prepending the
// synthesized index.json entry. Entries must carry normalized '/'
// names already (see CollectHeaders).
std::vector<std::uint8_t> PackHeadersBlob(
    const std::vector<HeaderEntry>& entries,
    const std::string& include_root_label);

// Parses an XJSH blob back into entries (index.json included).
bool UnpackHeadersBlob(const std::vector<std::uint8_t>& blob,
                       std::vector<HeaderEntry>* entries,
                       std::string* err);

// Recursively collects *.h/*.hpp/*.hxx/*.inc under include_root into
// entries, names relative to the root ("libdemo/dsp.h"). Empty root
// or no headers is not an error — returns an empty list.
bool CollectHeaders(const std::string& include_root,
                    std::vector<HeaderEntry>* entries,
                    std::string* err);

// Writes every entry to dest_dir (created as needed). Names are
// validated: absolute paths / ".." / drive letters fail.
bool ExtractHeaders(const std::vector<HeaderEntry>& entries,
                    const std::string& dest_dir, std::string* err);

// ---- shared tool helpers (ztzmake + XSJOBuildX) -----------------------------

// Directory of the running executable — argv[0]-independent.
std::string ExeDir(const char* argv0);

// ve.core.h SDK include-dir candidates, in priority order; only
// directories that exist are returned. Same list ztzmake has always
// used: explicit flag, $XER_VE_SDK, <exe>/sdk layouts, then the
// conventional in-tree locations relative to the input dir.
std::vector<std::string> ResolveSdkDirs(const std::string& exe_dir,
                                        const std::string& input_dir,
                                        const std::string& explicit_sdk);

// Extracts "key":"value" from a flat JSON object (best-effort
// scanner — used on our own emitted headers). "" when absent.
std::string JsonGetString(const std::string& json,
                          const std::string& key);

// Escapes a string for embedding in JSON output.
std::string JsonEscape(const std::string& s);

}  // namespace XER::Xejs
