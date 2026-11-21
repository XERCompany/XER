// xsjo_jit.h — ORC LLJIT host for the mobjective/*.xsjo payloads a
// .xexrj extension archive can carry.
//
// .xsjo container (produced by ztzmake/xsjo_compile.cc — all
// integers little-endian):
//   [4]   magic "XSJ1"
//   [u16] header_version (=1)
//   [u16] triple_len   [triple bytes]    — "x86_64-w64-windows-gnu"
//   [u16] name_len     [name bytes]      — original source filename
//   [u64] payload_size
//   [payload_size bytes] COFF object (MinGW x86-64)
// When the magic is absent but the buffer opens with a COFF machine
// tag the WHOLE buffer is treated as the object — bare .obj files
// load through the same path, which keeps the loader usable on
// hand-built objects during bring-up.
//
// Usage:
//   auto jit = XsjoJit::Create(&err);
//   jit->LoadModule(bytes, size, "mod", HostSymbols(), &module_id);
//   jit->CallEntry(module_id, "ve_ext_entry", ctx, &rc);
//   jit->UnloadModule(module_id);
//
// Symbol resolution is deterministic: every module gets its own
// JITDylib and the caller's ve_* implementations are DEFINED there
// as absolute addresses before the object links. The JIT never falls
// back to process symbol lookup — an object referencing an
// un-provided symbol fails to resolve loudly instead of binding a
// random host function.
//
// LLVM is an implementation detail of the .cc: this header is
// LLVM-free so consumers (and the no-LLVM stub build) need nothing
// from the toolchain. Errors inside are llvm::Expected and surface
// here as Result{ok,error}; Available()/Create() report "built
// without LLVM" / "libLLVM-23.dll missing" cleanly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Forward-declared so the private section needs no LLVM headers.
namespace llvm::orc { class JITDylib; }

namespace XER::VideoEditor {

class XsjoJit {
public:
    // One host-provided absolute symbol: the RAW object-file name
    // ("ve_log" — no leading underscore on x64 COFF) mapped to the
    // address of the host implementation.
    struct HostSymbol {
        std::string name;
        void* address = nullptr;
    };

    struct Result {
        bool ok = false;
        std::string error;
        explicit operator bool() const { return ok; }
        static Result Ok() { return {true, {}}; }
        static Result Fail(std::string e) {
            return {false, std::move(e)};
        }
    };

    // False when this binary was built without LLVM or the delay-
    // loaded libLLVM DLL cannot be found — callers report a clean
    // "JIT unavailable" instead of crashing.
    static bool Available();

    // Initializes the native target once (std::call_once) and builds
    // the LLJIT instance. nullptr + `error` on failure.
    static std::unique_ptr<XsjoJit> Create(std::string* error);

    ~XsjoJit();
    XsjoJit(const XsjoJit&) = delete;
    XsjoJit& operator=(const XsjoJit&) = delete;

    // Parses the XSJ1 header, validates the embedded object, creates a
    // dedicated JITDylib, defines `host_symbols` in it as absolute
    // addresses and links the object. `module_out` receives the id
    // used by every other call.
    Result LoadModule(const void* xsjo_bytes, std::size_t size,
                      const std::string& name,
                      const std::vector<HostSymbol>& host_symbols,
                      std::uint64_t* module_out);

    // Unmangled-name lookup inside the module's JITDylib; the address
    // of the (now materialized) symbol lands in `addr_out`.
    Result FindSymbol(std::uint64_t module, const std::string& name,
                      void** addr_out);

    // Calls `name` as `int fn(void* ctx)` — the ve_ext_entry
    // convention. The function's return value lands in `rc_out`.
    Result CallEntry(std::uint64_t module, const std::string& name,
                     void* ctx, int* rc_out);

    // Calls `name` as `const char* fn(void* ctx, const char*
    // args_json)` — the exported-callable convention behind ext_call.
    // The returned string is copied out; per ve.core.h it must point
    // to extension-owned storage and is never freed by the host.
    Result CallJson(std::uint64_t module, const std::string& name,
                    void* ctx, const std::string& args_json,
                    std::string* result_out);

    // Removes the module's JITDylib and frees its code.
    Result UnloadModule(std::uint64_t module);

private:
    struct Impl;
    explicit XsjoJit(std::unique_ptr<Impl> impl);
    // The module's dedicated JITDylib, or nullptr for an unknown id.
    llvm::orc::JITDylib* Module(std::uint64_t module);
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::VideoEditor
