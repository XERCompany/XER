// hello.cc — demo extension native payload (tools/tz/xsjo_compile.*).
//
// ztzmake compiles this file in-process (Clang frontend) into
// mobjective/hello.xsjo inside demo.xexrj; VE.ExtensionsRuntime JIT-
// links the COFF payload and binds the ve_* imports to its host
// symbol map. IMPORTANT: only ve_* host symbols plus the extension's
// own meshed modules are resolved — this TU is deliberately
// freestanding (no libc calls at all: no memcpy, strlen, snprintf),
// because any undefined CRT reference would fail the JIT link. Keep
// it that way.
//
// Library chain: this TU also calls into libdemo (a .xejslib
// compiled by XSJOBuildX and consumed by ztzmake --dep / libs/
// drop-in). Its modules land as mobjective/lib_*.xsjo next to this
// one and the JIT meshes the extension's dylibs — the mangled
// libdemo:: references below resolve exactly like a static link.
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

// libdemo headers ride the .xejslib's .xejshead bundle — unpacked
// onto -I by ztzmake when the dep is consumed. Absent the dep, the
// export below degrades to a "no libdemo" note instead of a pack
// failure.
#if defined(__has_include)
#  if __has_include("libdemo/dsp.h")
#    include "libdemo/dsp.h"
#    define DEMO_HAVE_LIBDEMO 1
#  endif
#endif

#if !defined(DEMO_HAVE_VE_CORE)
// Minimal forward decls (keep in sync with ve.core.h).
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VE_CORE_API_VERSION 3
enum { VE_OK = 0, VE_ERR_GENERIC = 1 };
typedef struct ve_ctx ve_ctx;
void ve_log(ve_ctx* ctx, const char* message);
void ve_set_status(ve_ctx* ctx, const char* message);
int ve_project_info(ve_ctx* ctx, char** out_json);
int ve_add_clip(ve_ctx* ctx, const char* kind, const char* param,
                double start_seconds, double duration_seconds);
int ve_call(ve_ctx* ctx, const char* name, const char* args_json,
            char** out_json);
void ve_post(ve_ctx* ctx, const char* event_json);
void ve_free(ve_ctx* ctx, void* ptr);
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

// Callable through ext_call: asks the host to place a clip. The
// request queues as an "add_clip" event (fire-and-forget bridge —
// the editor applies it on its next pump); this export exists so
// the smoke suite can drive and inspect that event shape.
const char* ve_demo_add_clip(ve_ctx* ctx, const char* args_json) {
    (void)args_json;
    const int rc =
        ve_add_clip(ctx, "color", "{\"color\":\"#22AA88\"}", 1.0, 2.5);
    return rc == VE_OK ? "{\"ok\":true,\"queued\":\"add_clip\"}"
                       : "{\"ok\":false}";
}

// Callable through ext_call: exercises the libdemo .xejslib chain —
// GainDbQ15 / ApplyGainQ15 / ClampI32 / LerpQ15 / Version are
// mangled-name references resolved out of mobjective/lib_dsp.xsjo at
// JIT time (meshed link order). The result reports the actual
// computed values so the smoke suite can assert the lib really ran,
// not just that some symbol bound.
const char* ve_demo_gain(ve_ctx* ctx, const char* args_json) {
    (void)ctx;
    (void)args_json;
    static char buf[512];
#if defined(DEMO_HAVE_LIBDEMO)
    const int gain_q15 = libdemo::GainDbQ15(-6);
    const int out = libdemo::ApplyGainQ15(12000, gain_q15);
    const int clamped = libdemo::ClampI32(99999, 0, 1000);
    const int lerped = libdemo::LerpQ15(0, 1000, 16384);
    const char* ver = libdemo::Version();
    char* w = buf;
    const char* const end = buf + sizeof(buf) - 2;
    auto put_str = [&](const char* s) {
        while (*s && w < end) *w++ = *s++;
    };
    auto put_int = [&](int v) {
        char tmp[12];
        char* t = tmp + sizeof(tmp);
        unsigned u = v < 0 ? 0u - static_cast<unsigned>(v)
                           : static_cast<unsigned>(v);
        *--t = '\0';
        do {
            *--t = static_cast<char>('0' + (u % 10));
            u /= 10;
        } while (u != 0);
        if (v < 0) *--t = '-';
        put_str(t);
    };
    put_str("{\"ok\":true,\"lib\":\"");
    put_str(ver);
    put_str("\",\"gain_q15\":");
    put_int(gain_q15);
    put_str(",\"in\":12000,\"out\":");
    put_int(out);
    put_str(",\"clamped\":");
    put_int(clamped);
    put_str(",\"lerped\":");
    put_int(lerped);
    put_str("}");
    *w = '\0';
#else
    const char kNoLib[] =
        "{\"ok\":false,\"error\":\"packed without libdemo dep\"}";
    int i = 0;
    while (kNoLib[i]) { buf[i] = kNoLib[i]; ++i; }
    buf[i] = '\0';
#endif
    return buf;
}

// Callable through ext_call: drives ve_call — the synchronous
// editor-service bridge (v2). Whatever the host answered is echoed
// back verbatim so callers can inspect the whole round trip.
const char* ve_demo_hostcall(ve_ctx* ctx, const char* args_json) {
    (void)args_json;
    static char buf[1024];
    static const char kPrefix[] = "{\"ok\":true,\"reply\":";
    static const char kNull[] = "null}";
    char* reply = nullptr;
    const int rc = ve_call(ctx, "project.info", "{}", &reply);
    char* out = buf;
    for (const char* p = kPrefix; *p && out < buf + sizeof(buf) - 2;
         ++p, ++out) {
        *out = *p;
    }
    if (rc == VE_OK && reply != nullptr) {
        for (const char* p = reply;
             *p && out < buf + sizeof(buf) - 2; ++p, ++out) {
            *out = *p;
        }
        *out++ = '}';
        *out = '\0';
    } else {
        for (const char* p = kNull; *p; ++p, ++out) *out = *p;
        *out = '\0';
    }
    if (reply != nullptr) ve_free(ctx, reply);
    return buf;
}

// Callable through ext_call: the ve.core v3 table — asks the editor
// for its served-service list ("ve.services") and echoes the answer
// verbatim, so callers can diff the advertised table against the
// documented one in sdk/ve.core.h.
const char* ve_demo_services(ve_ctx* ctx, const char* args_json) {
    (void)args_json;
    static char buf[4096];
    static const char kPrefix[] = "{\"ok\":true,\"reply\":";
    static const char kNull[] = "null}";
    char* reply = nullptr;
    const int rc = ve_call(ctx, "ve.services", "{}", &reply);
    char* out = buf;
    for (const char* p = kPrefix; *p && out < buf + sizeof(buf) - 2;
         ++p, ++out) {
        *out = *p;
    }
    if (rc == VE_OK && reply != nullptr) {
        for (const char* p = reply;
             *p && out < buf + sizeof(buf) - 2; ++p, ++out) {
            *out = *p;
        }
        *out++ = '}';
        *out = '\0';
    } else {
        for (const char* p = kNull; *p; ++p, ++out) *out = *p;
        *out = '\0';
    }
    if (reply != nullptr) ve_free(ctx, reply);
    return buf;
}

}  // extern "C"
