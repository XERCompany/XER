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
 * it defines itself. v3 kept the same seven functions — the entire
 * "full program control" growth rides on ve_call names (see the
 * service table below), not on ABI expansion.
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
 *
 * ---- the ve_call service table (v3) -----------------------------------
 * ve_call(name, args_json) is the ONE generic entry into editor
 * services; v3 expands that name space to full program control. The
 * SAME table serves both extension kinds:
 *   - native (.xsjo JIT): ve_call -> host_call reverse channel ->
 *     the editor's UI-thread dispatcher (ext_runtime_server.cc).
 *   - JS (main.js): xer_ext.ve(name, args) routes in-process to the
 *     identical dispatcher — no pipe involved (ext_host.cc).
 *
 * Result contract: every service answers a JSON object. Success is
 * {"ok":true, ...payload}; failure is
 * {"ok":false,"error":"<machine token>","detail"?:...}. A name the
 * host does not implement answers {"ok":false,"error":"unknown_call"}
 * — extensions can enumerate the served names ahead of time via the
 * "ve.services" service.
 *
 * Argument conventions: args_json is always a JSON OBJECT ({} when
 * the call takes none). Times are timeline seconds, ids are the
 * clip/track string ids reported by the describe calls, sizes are
 * pixels. Mutation calls funnel through the editor's undoable
 * history — Ctrl+Z reverses anything the table does to the project.
 *
 *   ve.services                → {"ok","version":3,"services":[names]}
 *   app.info                   → {"ok","role","api","project_path",
 *                                 "native_extensions":[ids]}
 *
 *   project.* — file-level control:
 *     project.info             → canvas/fps/track+clip counts/
 *                                duration/path/dirty (live, not the
 *                                load-time snapshot ve_project_info
 *                                falls back to)
 *     project.path             → {"path","dirty","untitled"}
 *     project.new {force?}     → fresh default project; refuses with
 *                                {"ok":false,"error":"unsaved_changes"}
 *                                while dirty unless force:true
 *     project.open {path,force?} → load a .xerproj; same dirty guard
 *     project.save {path?}     → save; "no_project_path" when the
 *                                project has never been saved and no
 *                                path was passed (never opens a dialog)
 *     project.settings {w?,h?,fps_num?,fps_den?} → canvas/fps edit
 *                                (undoable)
 *     project.markers          → {"markers":[{time,name,color}]}
 *     project.add_marker {t,name?}   / remove_marker {t} /
 *     rename_marker {t,name} / set_work_area {in,out} — undoable
 *
 *   timeline.* — structure + edit ops (all undoable):
 *     timeline.info            → playhead/duration/counts/selection
 *     timeline.tracks          → [{index,kind,name,muted,hidden,
 *                                locked,gain_db,pan,clips}]
 *     timeline.clips           → flat clip list (+ project.clips
 *                                stays a v2 alias)
 *     timeline.transitions     → [{from,to,type,duration,script?}]
 *     timeline.add_track {kind:"video"|"audio",name?} → {"index"}
 *     timeline.remove_track {index}
 *     timeline.rename_track {index,name}
 *     timeline.set_track {index,muted?,hidden?,locked?,gain_db?,
 *                         pan?,name?}          — one undo step
 *     timeline.add_clip {kind,param?,start?,duration?,track?} — the
 *                                confirmed sibling of ve_add_clip
 *     timeline.move_clip {id,start?,track?}
 *     timeline.remove_clip {id,ripple?}
 *     timeline.trim_clip {id,edge:"in"|"out",point}
 *     timeline.split_clip {id,at}                → {"new_clip_id"}
 *     timeline.ripple_delete {id}
 *     timeline.lift_range {t0,t1,close_gap}
 *     timeline.slip {id,delta} / timeline.slide {id,delta}
 *     timeline.set_rate {id,rate}
 *     timeline.nudge {frames:int}                — selected clip
 *     timeline.select_clip {id|""}               — selection.set alias
 *     timeline.add_transition {from,to?,after?,type,duration?,
 *                              script?}   type: the .xerproj names
 *                              (crossfade, wipe_left, ..., script);
 *                              "after" picks the next clip on the
 *                              track when "to" is omitted
 *     timeline.remove_transition {from,to}
 *
 *   transport.* — playback:
 *     transport.info           → {"playing","playhead","rate","loop",
 *                                 "loop_in","loop_out","duration"}
 *     transport.play / pause / stop / seek {t}        (v2 set)
 *     transport.frame_step {frames:int}             — ±n frames
 *     transport.set_loop {on,in?,out?}              — monitor loop
 *     transport.set_rate {rate}                     — 0 = pause
 *     transport.shuttle {direction:±1} / shuttle_stop — JKL ladder
 *     transport.goto_marker {next:bool}
 *     transport.set_quality {scale:0..1}            — preview res
 *
 *   selection.*:
 *     selection.get            → {"clip_id","clip"?,"media_path"}
 *     selection.set {clip_id?,media?} / selection.clear
 *
 *   media.* — the media bin/catalog:
 *     media.list               → {"entries":[{id,path,duration,w,h,
 *                                fps,has_audio,poster}]}
 *     media.import {path}      → probe + catalog upsert → {"id"}
 *     media.refresh            → rescan → {"count"}
 *     media.select {path}      — bin selection
 *     media.add_to_timeline {path,at?,track?}
 *     media.bounds {path}      → {"duration_seconds","has_video"}
 *
 *   fx.* — clip effect chains:
 *     fx.list {clip?}          → the built-in effect catalog, or the
 *                                named clip's chain when "clip" given
 *     fx.add {clip,type,params?}                      — undoable
 *     fx.remove {clip,index?} / fx.clear {clip}       — undoable
 *
 *   export.* — the offline export engine:
 *     export.presets           → builtin + user presets
 *     export.start {output,video_codec?,audio_codec?,
 *                   video_bit_rate?,audio_bit_rate?,
 *                   resolution_scale?,range_in?,range_out?,preset?}
 *     export.cancel            → cooperative cancel
 *     export.status            → {"status","running","progress":{
 *                                fraction,stage,...},"error"?,"jobs"}
 *
 *   ui.* — presentation (never blocks on a native dialog):
 *     ui.status {text,is_error?}                     — status strip
 *     ui.open_panel {tab:name|index}                 — right dock tab
 *     ui.list_extensions         → scanned .xexrj entries
 *     ui.open_extension_ui {id}  → xer-ext:// page in the Ext tab
 *     ui.run_extension {id}      → re-run a verified extension's
 *                                entry point (synchronous)
 *     ui.rescan_extensions       → rescan assets/extensions/
 *
 *   edit.* — history:
 *     edit.undo / edit.redo / edit.history → undo/redo + label stacks
 *
 * v2 aliases kept for compatibility: "project.clips", "status".
 * "app.quit" deliberately does NOT exist — extensions may never take
 * the process down.
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
 *       error). No signatures changed in v2.
 *   3 — full program control lands through the SAME ve_call
 *       dispatch: the service table documented above (project.*,
 *       timeline.*, transport.*, selection.*, media.*, fx.*,
 *       export.*, ui.*, edit.*) is served both to native extensions
 *       over the reverse channel and to JS extensions in-process via
 *       xer_ext.ve(). No new C functions were added — ve_call is the
 *       single generic entry, so every v1/v2 extension binary keeps
 *       working and only the served name space grew. ve_project_info
 *       upgrades opportunistically: with the channel up it answers
 *       the LIVE project instead of the load-time snapshot. */
#define VE_CORE_API_VERSION 3

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

/* Snapshot of the open project as a JSON string [host-alloc]. v3:
 * when the reverse host channel is up the runtime forwards this as
 * ve_call("project.info") — a LIVE project description — and only a
 * host without the channel returns the static snapshot the editor
 * attached to the load request; a stub may return whatever fixture
 * it likes. Returns VE_OK and sets *out_json (free with ve_free). */
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
