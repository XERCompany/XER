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

/* Version history:
 *   1 — initial ABI; ve_add_clip/ve_call only queue events, ve_call
 *       answers {"ok":false,"error":"not bridged yet"}.
 *   2 — ve_call is bridged synchronously: when the runtime was
 *       spawned with the reverse host channel (worker_pipe
 *       callback_env, XER_VE_EXT_HOST_* handles), ve_call blocks in
 *       the runtime until the editor's service dispatcher answers.
 *       A host spawned without the channel keeps the v1 fallback, so
 *       extensions built against v1 keep working unchanged — only the
 *       result JSON differs (a real answer instead of the stub
 *       error). No signatures changed in v2. */
#define VE_CORE_API_VERSION 2

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
 * seconds. The request is deliberately QUEUED as an "add_clip"
 * event the editor fulfills asynchronously (unchanged in v2 — the
 * user-visible placement is acknowledgement enough); VE_OK means
 * accepted for delivery, not necessarily applied. Extensions that
 * need confirmation use ve_call("timeline.add_clip", ...) instead. */
int ve_add_clip(ve_ctx* ctx, const char* kind, const char* param,
                double start_seconds, double duration_seconds);

/* Call a named editor service synchronously. name is a service id
 * (e.g. "project.info"); args_json is a JSON argument object.
 * Returns VE_OK and a JSON result string [host-alloc] in *out_json;
 * the result may itself carry {"ok":false,...} when the service
 * failed or is unknown.
 *
 * v2 (VE.ExtensionsRuntime spawned by the editor): the call travels
 * the reverse host channel — the runtime writes a "host_call"
 * request and BLOCKS inside ve_call until the editor's service
 * dispatcher answers on the reply pipe. This is deadlock-free only
 * because the editor services host_call from its UI tick while the
 * triggering request runs on a background job; see
 * ext_runtime_server.cc for the full channel contract.
 *
 * v1 fallback (host spawned without the channel, e.g. an in-editor
 * stub): the call is queued as a "call_request" event and ve_call
 * returns {"ok":false,"error":"not bridged yet"} — accepted for
 * delivery, never answered synchronously. */
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
