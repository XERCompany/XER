/* ve.core.h — XER VideoEditor native-extension core ABI.
 *
 * This header is the STABLE, published contract between the editor
 * and native extensions. It is pure C (C99-compatible, extern "C"
 * throughout) — no STL and no C++ types ever cross the boundary.
 *
 * A native extension ships as COFF objects packed under the
 * mobjective/ dir as .xsjo members of a .xexrj archive. The
 * out-of-process
 * runtime (VE.ExtensionsRuntime.exe) JIT-links those objects with
 * ORC LLJIT and resolves their undefined ve_* symbols against the
 * host implementation described below — deterministic absolute
 * addresses handed to the JIT, never process symbol lookup.
 *
 * Two directions share this file:
 *   ve_log / ve_set_status / ve_project_info / ve_add_clip /
 *   ve_call / ve_post / ve_free — implemented BY THE HOST, called
 *                                 by the extension.
 *   ve_ext_entry / ve_ext_shutdown / any exported callable —
 *                                 implemented BY THE EXTENSION,
 *                                 looked up and invoked by the host.
 *
 * The whole surface is deliberately small so it can be implemented
 * twice: once by the dedicated runtime process (the real bridge into
 * the editor over the JSON-lines worker protocol) and once by a
 * lightweight in-editor stub for testing extension code without a
 * subprocess or a JIT. A stub only needs this header and a ve_ctx
 * it defines itself.
 *
 * Conventions (v1, VE_CORE_API_VERSION):
 *   - All strings are UTF-8 and NUL-terminated; out parameters are
 *     never NULL on entry.
 *   - Return codes: 0 on success (VE_OK), non-zero on failure.
 *   - Host->extension strings marked [host-alloc] are allocated by
 *     the host and released with ve_free(). Extension->host strings
 *     returned by exported callables must reference extension-owned
 *     storage valid until the next call — the host copies them
 *     immediately and NEVER frees them (a literal works, a malloc'd
 *     buffer leaks but never crashes).
 */
#ifndef VE_CORE_H
#define VE_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VE_CORE_API_VERSION 1

enum {
    VE_OK = 0,              /* success */
    VE_ERR_GENERIC = 1,     /* unspecified failure */
    VE_ERR_UNAVAILABLE = 2, /* service not implemented in this host */
    VE_ERR_BAD_ARG = 3      /* malformed argument */
};

/* Opaque context — one per loaded extension, created by the host at
 * load time and passed back on every call. Extensions must not
 * inspect it; the host binds it to the extension's request channel
 * (event queue, project snapshot, editor bridge). */
typedef struct ve_ctx ve_ctx;

/* ------------------------- host services ------------------------- */
/* Implemented by the runtime (or an in-editor stub); resolved by the
 * JIT through the host symbol map under exactly these names. */

/* Append one line to the extension log. Fire-and-forget: the runtime
 * forwards it to the editor as a "log" event. */
void ve_log(ve_ctx* ctx, const char* message);

/* Update the editor status line for this extension. Forwarded to the
 * editor as a "status" event. */
void ve_set_status(ve_ctx* ctx, const char* message);

/* Snapshot of the open project as a JSON string [host-alloc]. The
 * runtime returns the static snapshot the editor attached to the
 * load request; a stub may return whatever fixture it likes.
 * Returns VE_OK and sets *out_json (free with ve_free). */
int ve_project_info(ve_ctx* ctx, char** out_json);

/* Ask the editor to create a clip. kind is a clip kind token
 * ("media", "color", "bars", "text", ...); param is a JSON string
 * with kind-specific parameters; start/duration are timeline
 * seconds. In v1 the request is QUEUED as an "add_clip" event the
 * editor fulfills asynchronously — VE_OK means accepted for
 * delivery, not necessarily applied. */
int ve_add_clip(ve_ctx* ctx, const char* kind, const char* param,
                double start_seconds, double duration_seconds);

/* Call a named editor service synchronously. name is a service id
 * (e.g. "timeline.info"); args_json is a JSON argument object.
 * Returns VE_OK and a JSON result string [host-alloc] in *out_json;
 * the result may itself carry {"ok":false,...} when the service is
 * not bridged — v1 queues the call as a "call_request" event and
 * returns {"ok":false,"error":"not bridged yet"}. A future protocol
 * version will route this through a dedicated request channel so the
 * editor can answer synchronously (see ext_runtime_server.cc). */
int ve_call(ve_ctx* ctx, const char* name, const char* args_json,
            char** out_json);

/* Post an arbitrary JSON event to the editor (extension-defined
 * event objects — progress, diagnostics, UI nudges). event_json must
 * be a JSON object or array; it is wrapped in {"type":"event",
 * "event":...} on the wire. Fire-and-forget. */
void ve_post(ve_ctx* ctx, const char* event_json);

/* Release a string the host handed to this extension (out_json
 * results). Passing NULL is a no-op. */
void ve_free(ve_ctx* ctx, void* ptr);

/* --------------------- extension-side exports -------------------- */
/* Symbols the host looks up in the extension's JIT modules. They do
 * NOT need dllexport/visibility annotations — the JIT sees every
 * global symbol in the object — but they must have C linkage so the
 * object file spells them without C++ mangling. */

/* Required for native extensions: called once after every module is
 * loaded. Return VE_OK on success; any other code reports the
 * failure to the editor (the extension stays loaded — the host
 * relays entry_rc so the editor can decide to unload). */
int ve_ext_entry(ve_ctx* ctx);

/* Optional: called once before the extension's modules are removed
 * (ext_unload or process teardown). Absence is not an error. */
void ve_ext_shutdown(ve_ctx* ctx);

/* Optional: the API version the extension was built against
 * (VE_CORE_API_VERSION). Absence means "version 1". */
int ve_ext_api_version(void);

/* Arbitrary exported callables reachable through the ext_call
 * request: `const char* fn(ve_ctx*, const char* args_json)`. The
 * returned JSON string must reference extension-owned storage valid
 * until the next call — the host copies it immediately and never
 * frees it (a string literal works). NULL reports an empty result. */
typedef const char* (*ve_callable_fn)(ve_ctx* ctx,
                                      const char* args_json);
typedef int (*ve_entry_fn)(ve_ctx* ctx);
typedef void (*ve_shutdown_fn)(ve_ctx* ctx);
typedef int (*ve_api_version_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* VE_CORE_H */
