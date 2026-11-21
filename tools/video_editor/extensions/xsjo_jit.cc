// xsjo_jit.cc — see xsjo_jit.h for the container layout and the
// symbol-resolution contract. Compiled ONLY into the
// VE.ExtensionsRuntime target (excluded from the core glob in
// CMakeLists.txt) so the editor and the other workers never link
// LLVM. With XER_VE_HAS_LLVM=0 every method degrades to a clean
// "built without LLVM" Result.
#include "xsjo_jit.h"

#if XER_VE_HAS_LLVM

#include <cstdlib>
#include <mutex>
#include <unordered_map>

#include <llvm/ExecutionEngine/Orc/AbsoluteSymbols.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/TargetSelect.h>

#ifdef _WIN32
#include <windows.h>   // LoadLibraryA probe for the delay-loaded DLL
#endif

// Defined by CMake to the actual runtime name ("libLLVM-23.dll").
#ifndef XER_VE_LLVM_DLL
#define XER_VE_LLVM_DLL "libLLVM-23.dll"
#endif

namespace XER::VideoEditor {
namespace {

std::once_flag g_llvm_init;

void InitLLVMOnce() {
    std::call_once(g_llvm_init, [] {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
    });
}

std::string ToString(llvm::Error err) {
    return llvm::toString(std::move(err));
}

// Reads little-endian integers.
std::uint16_t ReadU16(const char* p) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(
             static_cast<unsigned char>(p[0])) << 0) |
        (static_cast<std::uint16_t>(
             static_cast<unsigned char>(p[1])) << 8));
}
std::uint64_t ReadU64(const char* p) {
    std::uint64_t x = 0;
    for (unsigned s = 0; s < 64; s += 8) {
        x |= static_cast<std::uint64_t>(
                 static_cast<unsigned char>(p[s / 8]))
             << s;
    }
    return x;
}

bool LooksLikeCoff(llvm::StringRef bytes) {
    if (bytes.size() < 2) return false;
    const auto machine = static_cast<unsigned>(
        (static_cast<unsigned char>(bytes[0])) |
        (static_cast<unsigned char>(bytes[1]) << 8));
    // IMAGE_FILE_MACHINE_AMD64 / I386 — the only objects this host
    // can run anyway; anything else fails validation downstream.
    return machine == 0x8664 || machine == 0x014c;
}

llvm::Error Err(const std::string& msg) {
    return llvm::make_error<llvm::StringError>(
        msg, llvm::inconvertibleErrorCode());
}

struct XsjoObject {
    llvm::StringRef bytes;    // view into the caller's buffer
    std::string triple;       // "" for bare .obj input
    std::string source_name;
};

// Splits an .xsjo blob into its embedded object bytes — mirrors
// tools/tz/xsjo_compile.cc's ParseXsjo (kept as a private copy so the
// runtime needs no ztzmake code): magic, u16 version, two
// length-prefixed strings, u64 payload size, payload.
llvm::Expected<XsjoObject> XsjoParse(llvm::StringRef bytes) {
    const char* p = bytes.data();
    const std::size_t n = bytes.size();
    if (n >= 4 && bytes.substr(0, 4) == "XSJ1") {
        if (n < 6) return Err("truncated XSJ1 header");
        if (ReadU16(p + 4) != 1) {
            return Err("unsupported XSJ1 version");
        }
        std::size_t at = 6;
        XsjoObject out;
        const auto read_str = [&](std::string* s) -> bool {
            if (at + 2 > n) return false;
            const std::uint16_t len = ReadU16(p + at);
            at += 2;
            if (at + len > n) return false;
            s->assign(p + at, len);
            at += len;
            return true;
        };
        if (!read_str(&out.triple) ||
            !read_str(&out.source_name)) {
            return Err("truncated XSJ1 strings");
        }
        if (at + 8 > n) return Err("truncated XSJ1 payload size");
        const std::uint64_t payload_size = ReadU64(p + at);
        at += 8;
        if (payload_size > n - at) {
            return Err("XSJ1 payload overruns the member");
        }
        out.bytes = bytes.substr(
            at, static_cast<std::size_t>(payload_size));
        if (!LooksLikeCoff(out.bytes)) {
            return Err("XSJ1 payload does not start with a COFF "
                       "object");
        }
        return out;
    }
    if (LooksLikeCoff(bytes)) {
        XsjoObject out;
        out.bytes = bytes;   // bare .obj — tolerated, see .h
        return out;
    }
    return Err("not an .xsjo (missing XSJ1 magic or COFF header)");
}

}  // namespace

struct XsjoJit::Impl {
    std::unique_ptr<llvm::orc::LLJIT> jit;
    std::uint64_t next_module = 1;
    // module id → its dedicated JITDylib. Removal goes through
    // ExecutionSession::removeJITDylib which frees the module's code.
    std::unordered_map<std::uint64_t, llvm::orc::JITDylib*> modules;
};

XsjoJit::XsjoJit(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
XsjoJit::~XsjoJit() = default;

bool XsjoJit::Available() {
#ifdef _WIN32
    // libLLVM is eagerly bound (its __declspec(dllimport) data
    // symbols cannot be delay-loaded) — a missing DLL stops process
    // start entirely, but keep the probe so deployments that shadow
    // the import still report a clean reason here.
    if (LoadLibraryA(XER_VE_LLVM_DLL) == nullptr) {
        return false;
    }
#endif
    return true;
}

std::unique_ptr<XsjoJit> XsjoJit::Create(std::string* error) {
    InitLLVMOnce();
    if (!Available()) {
        if (error) {
            *error = std::string(XER_VE_LLVM_DLL) +
                     " not found (not staged beside the exe?)";
        }
        return nullptr;
    }
    auto jit_or = llvm::orc::LLJITBuilder().create();
    if (!jit_or) {
        if (error) *error = ToString(jit_or.takeError());
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->jit = std::move(*jit_or);
    return std::unique_ptr<XsjoJit>(new XsjoJit(std::move(impl)));
}

XsjoJit::Result XsjoJit::LoadModule(
    const void* xsjo_bytes, std::size_t size, const std::string& name,
    const std::vector<HostSymbol>& host_symbols,
    std::uint64_t* module_out) {
    if (module_out == nullptr) {
        return Result::Fail("module_out is null");
    }
    if (xsjo_bytes == nullptr || size == 0) {
        return Result::Fail("empty .xsjo payload");
    }
    const llvm::StringRef blob(
        static_cast<const char*>(xsjo_bytes), size);
    auto obj_or = XsjoParse(blob);
    if (!obj_or) {
        return Result::Fail(ToString(obj_or.takeError()));
    }

    // The object must target the same triple the JIT was built for —
    // a mismatched ABI (e.g. MSVC COFF vs MinGW) links but calls
    // functions with the wrong convention. Empty triple means a bare
    // .obj — the COFF machine tag is the only check.
    const std::string jit_triple =
        impl_->jit->getTargetTriple().str();
    if (!obj_or->triple.empty() && obj_or->triple != jit_triple) {
        return Result::Fail("XSJ1 triple '" + obj_or->triple +
                            "' does not match the JIT target '" +
                            jit_triple + "'");
    }

    // Copy so the buffer outlives the caller's archive bytes.
    auto buffer =
        llvm::MemoryBuffer::getMemBufferCopy(obj_or->bytes, name);

    // Validate it is a real object BEFORE handing it to the linking
    // layer — a malformed blob must fail here, not inside jitlink.
    {
        auto obj_or2 = llvm::object::ObjectFile::createObjectFile(
            buffer->getMemBufferRef());
        if (!obj_or2) {
            return Result::Fail(
                "embedded payload is not a loadable object: " +
                ToString(obj_or2.takeError()));
        }
    }

    auto& es = impl_->jit->getExecutionSession();
    const std::uint64_t id = impl_->next_module++;
    auto jd_or =
        impl_->jit->createJITDylib(name + "." + std::to_string(id));
    if (!jd_or) {
        return Result::Fail(ToString(jd_or.takeError()));
    }
    llvm::orc::JITDylib& jd = *jd_or;

    // Deterministic host binding: define the caller's ve_* symbols IN
    // this module's dylib as absolute addresses. The object's
    // undefined ve_* references resolve against the dylib's own
    // symbol table (searched before its link order) — never through
    // a process-wide DynamicLibrarySearchGenerator.
    if (!host_symbols.empty()) {
        llvm::orc::SymbolMap map;
        for (const HostSymbol& sym : host_symbols) {
            if (sym.name.empty() || sym.address == nullptr) continue;
            map[es.intern(sym.name)] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(sym.address),
                llvm::JITSymbolFlags::Exported);
        }
        if (auto err =
                jd.define(llvm::orc::absoluteSymbols(std::move(map)))) {
            llvm::consumeError(es.removeJITDylib(jd));
            return Result::Fail("host symbol define failed: " +
                                ToString(std::move(err)));
        }
    }

    if (auto err =
            impl_->jit->addObjectFile(jd, std::move(buffer))) {
        llvm::consumeError(es.removeJITDylib(jd));
        return Result::Fail("object link failed: " +
                            ToString(std::move(err)));
    }
    impl_->modules.emplace(id, &jd);
    *module_out = id;
    return Result::Ok();
}

llvm::orc::JITDylib* XsjoJit::Module(std::uint64_t module) {
    const auto it = impl_->modules.find(module);
    return it == impl_->modules.end() ? nullptr : it->second;
}

XsjoJit::Result XsjoJit::FindSymbol(std::uint64_t module,
                                    const std::string& name,
                                    void** addr_out) {
    if (addr_out == nullptr) return Result::Fail("addr_out is null");
    llvm::orc::JITDylib* jd = Module(module);
    if (jd == nullptr) {
        return Result::Fail("unknown module id " +
                            std::to_string(module));
    }
    auto addr_or = impl_->jit->lookup(*jd, name);
    if (!addr_or) {
        return Result::Fail(ToString(addr_or.takeError()));
    }
    *addr_out = addr_or->toPtr<void*>();
    return Result::Ok();
}

XsjoJit::Result XsjoJit::CallEntry(std::uint64_t module,
                                   const std::string& name, void* ctx,
                                   int* rc_out) {
    if (rc_out == nullptr) return Result::Fail("rc_out is null");
    void* addr = nullptr;
    Result found = FindSymbol(module, name, &addr);
    if (!found) return found;
    using EntryFn = int (*)(void*);
    *rc_out = reinterpret_cast<EntryFn>(addr)(ctx);
    return Result::Ok();
}

XsjoJit::Result XsjoJit::CallJson(std::uint64_t module,
                                  const std::string& name, void* ctx,
                                  const std::string& args_json,
                                  std::string* result_out) {
    if (result_out == nullptr) return Result::Fail("result_out is null");
    void* addr = nullptr;
    Result found = FindSymbol(module, name, &addr);
    if (!found) return found;
    using JsonFn = const char* (*)(void*, const char*);
    const char* reply = reinterpret_cast<JsonFn>(addr)(
        ctx, args_json.c_str());
    // ve.core.h: the result points to extension-owned storage — the
    // host copies it and NEVER frees it (a literal is legal).
    *result_out = reply != nullptr ? reply : "";
    return Result::Ok();
}

XsjoJit::Result XsjoJit::UnloadModule(std::uint64_t module) {
    llvm::orc::JITDylib* jd = Module(module);
    if (jd == nullptr) {
        return Result::Fail("unknown module id " +
                            std::to_string(module));
    }
    auto err = impl_->jit->getExecutionSession().removeJITDylib(*jd);
    impl_->modules.erase(module);
    if (err) {
        return Result::Fail(ToString(std::move(err)));
    }
    return Result::Ok();
}

}  // namespace XER::VideoEditor

#else   // !XER_VE_HAS_LLVM — the runtime exe still builds and answers
        // ping/info/ext_list; ext_load of native modules reports the
        // missing JIT cleanly.

namespace XER::VideoEditor {

struct XsjoJit::Impl {};

XsjoJit::XsjoJit(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
XsjoJit::~XsjoJit() = default;

bool XsjoJit::Available() { return false; }

std::unique_ptr<XsjoJit> XsjoJit::Create(std::string* error) {
    if (error) *error = "built without LLVM (XER_VE_HAS_LLVM=0)";
    return nullptr;
}

XsjoJit::Result XsjoJit::LoadModule(const void*, std::size_t,
                                    const std::string&,
                                    const std::vector<HostSymbol>&,
                                    std::uint64_t*) {
    return Result::Fail("built without LLVM");
}
XsjoJit::Result XsjoJit::FindSymbol(std::uint64_t, const std::string&,
                                    void**) {
    return Result::Fail("built without LLVM");
}
XsjoJit::Result XsjoJit::CallEntry(std::uint64_t, const std::string&,
                                   void*, int*) {
    return Result::Fail("built without LLVM");
}
XsjoJit::Result XsjoJit::CallJson(std::uint64_t, const std::string&,
                                  void*, const std::string&,
                                  std::string*) {
    return Result::Fail("built without LLVM");
}
XsjoJit::Result XsjoJit::UnloadModule(std::uint64_t) {
    return Result::Fail("built without LLVM");
}
llvm::orc::JITDylib* XsjoJit::Module(std::uint64_t) { return nullptr; }

}  // namespace XER::VideoEditor

#endif  // XER_VE_HAS_LLVM
