// hello.cc — demo extension native payload (tools/tz/xsjo_compile.*).
//
// ztzmake compiles this file in-process (Clang frontend) into
// mobjective/hello.xsjo inside demo.xexrj; VE.ExtensionsRuntime JIT-
// links the COFF payload and binds the ve_* imports to its host
// symbol map. IMPORTANT: only ve_* symbols are resolved — this TU is
// deliberately freestanding (no libc calls at all: no memcpy, strlen,
// snprintf), because any undefined CRT reference would fail the JIT
// link. Keep it that way.
//
// ve.core.h lives in tools/video_editor/sdk/ (resolved by ztzmake's
// --sdk / XER_VE_SDK / conventional paths). The __has_include
// fallback keeps this demo packable when the SDK dir is not on the
// include path — the forward decls mirror the header exactly.
#if defined(__has_include)
#  if __has_include("ve.core.h")
#    include "ve.core.h"
#    define DEMO_HAVE_VE_CORE 1
#  endif
#endif

#if !defined(DEMO_HAVE_VE_CORE)
// Minimal forward decls (keep in sync with ve.core.h).
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VE_CORE_API_VERSION 1
enum { VE_OK = 0, VE_ERR_GENERIC = 1 };
typedef struct ve_ctx ve_ctx;
void ve_log(ve_ctx* ctx, const char* message);
void ve_set_status(ve_ctx* ctx, const char* message);
#ifdef __cplusplus
}
#endif
#endif  // !DEMO_HAVE_VE_CORE

// ---- extension exports -------------------------------------------------
// C linkage is required: the host looks these names up unmangled in
// the JIT'd object. No dllexport needed — the JIT sees every global.

extern "C" {

int ve_ext_api_version(void) { return VE_CORE_API_VERSION; }

// Required: called once after the module is linked.
int ve_ext_entry(ve_ctx* ctx) {
    ve_set_status(ctx, "demo native module JIT-loaded");
    ve_log(ctx, "hello from mobjective/hello.xsjo (JIT COFF)");
    return VE_OK;
}

void ve_ext_shutdown(ve_ctx* ctx) {
    ve_log(ctx, "demo native module unloading");
}

// Callable through ext_call: the module's display name as a JSON
// string literal (extension-owned storage — legal per ve.core.h).
const char* ve_demo_name(ve_ctx* ctx, const char* args_json) {
    (void)ctx;
    (void)args_json;
    return "\"demo.hello (JIT)\"";
}

// Callable through ext_call: echoes the args JSON back inside a small
// envelope. The result must reference extension-owned storage valid
// until the next call — a static buffer is the simplest legal choice.
const char* ve_demo_echo(ve_ctx* ctx, const char* args_json) {
    (void)ctx;
    static char buf[1024];
    static const char kPrefix[] = "{\"ok\":true,\"echo\":";
    static const char kSuffix[] = "}";
    char* out = buf;
    for (const char* p = kPrefix; *p && out < buf + sizeof(buf) - 2;
         ++p, ++out) {
        *out = *p;
    }
    if (args_json != nullptr) {
        for (const char* p = args_json;
             *p && out < buf + sizeof(buf) - 2; ++p, ++out) {
            *out = *p;
        }
    }
    *out++ = kSuffix[0];
    *out = '\0';
    return buf;
}

}  // extern "C"
