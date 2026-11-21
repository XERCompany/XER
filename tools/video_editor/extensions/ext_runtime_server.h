// ext_runtime_server.h — the VE.ExtensionsRuntime worker-role entry:
// the out-of-process host for NATIVE extension payloads.
//
// Why a dedicated process: a .xexrj can carry mobjective/*.xsjo
// (XSJ1-wrapped COFF objects) that run JIT-linked machine code with
// host privileges — a crash there must kill a disposable worker, not
// the editor. The in-editor ExtHost keeps running the JS/static side
// (main.js, xer-ext:// UI); THIS process owns the native half and
// bridges it over the shared JSON-lines worker protocol.
//
// Wire protocol (one JSON object per line on stdin/stdout — same
// transport as media_worker_protocol.h; these kinds live outside the
// shared Kind enum so the loop parses "kind" itself):
//
//   {"id":N,"kind":"ping"}          → {"id","ok":true,"pong":true,
//                                     "role":"VE.ExtensionsRuntime"}
//   {"id":N,"kind":"info"}          → {"id","ok":true,"role":...,
//                                     "serves":"ext_load,ext_call,
//                                       ext_unload,ext_list,
//                                       ext_events,ping,info"}
//   {"id":N,"kind":"ext_list",
//     "dir":"<extensions dir>"}     → {"id","ok":true,"exts":[
//                                      {"path","id","name",
//                                       "has_native","has_static",
//                                       "error"?}]}
//     dir optional — defaults to <exe>/assets/extensions. has_native
//     is true when the archive holds mobjective/*.xsjo members.
//   {"id":N,"kind":"ext_load",
//     "path":"<abs .xexrj>",
//     "project_json":"<json>"?}     → {"id","ok":true,"ext_id","name",
//                                      "native":bool,"modules":M,
//                                      "entry_rc":R?,"events":[...]}
//     Opens the Tz archive, parses manifest.toml with the same parser
//     as ExtHost (ExtManifestParse), verifies with TzVerify, JIT-loads
//     every mobjective/*.xsjo into a per-extension JITDylib and calls
//     ve_ext_entry(ctx). JS-only archives (no mobjective members)
//     return ok:true with "native":false,"modules":0 — graceful
//     hand-off to the in-editor host. project_json is the static
//     snapshot ve_project_info() later reports.
//   {"id":N,"kind":"ext_call",
//     "ext_id":"...","fn":"<symbol>",
//     "args":"<json>"|{...}}        → {"id","ok":true,
//                                      "result_json":"<text>",
//                                      "events":[...]}
//     Invokes the exported callable `fn` as
//     `char* fn(ve_ctx*, const char* args_json)` — see sdk/ve.core.h.
//   {"id":N,"kind":"ext_unload",
//     "ext_id":"..."}               → {"id","ok":true}
//     Calls ve_ext_shutdown when exported, then removes the
//     extension's JITDylibs.
//   {"id":N,"kind":"ext_events",
//     "ext_id":"..."}?              → {"id","ok":true,"events":[...]}
//     Drains the queued event list (see below). ext_id optional —
//     omitted drains every extension's queue.
//
// Event envelope (the ve_* fire-and-forget bridge): every reply that
// ran extension code carries "events":[{...}] — log lines, status
// updates, posted events, and the queued add_clip/call requests the
// editor is expected to fulfill:
//   {"ext":"id","type":"log","msg":"..."}
//   {"ext":"id","type":"status","msg":"..."}
//   {"ext":"id","type":"event","event":{...}}        — ve_post
//   {"ext":"id","type":"add_clip","kind","param","start","duration"}
//   {"ext":"id","type":"call_request","name","args"} — ve_call
//                                                     (v1 fallback only)
//
// ---- the synchronous host channel (SDK v2) ------------------------
// ve_call/ve_add_clip CANNOT block for an answer on the main stdin/
// stdout pipe: extension code runs on the worker's single request
// thread, so the runtime could never read a second request carrying
// a reply — a same-channel synchronous bridge deadlocks by
// construction. The answer is a SECOND transport: when the editor's
// WorkerPipe spawns this role with spec.callback_env set, the child
// inherits two extra anonymous-pipe handles advertised through the
// environment:
//   XER_VE_EXT_HOST_REQ — runtime WRITES host_call request lines
//   XER_VE_EXT_HOST_RSP — runtime READS editor reply lines
//   {"kind":"host_call","req_id":N,"ext":"id","name":"service",
//    "args":"<json>"}            (runtime -> editor)
//   {"req_id":N,"ok":true,"result":"<json>"} |
//   {"req_id":N,"ok":false,"error":"..."}   (editor -> runtime)
// ve_call writes host_call then blocks reading the reply — legal
// only because this is a different transport from the request pipe
// the request thread is parked inside. The editor drains REQ on a
// dedicated reader thread and services requests from its UI tick,
// which is why every editor->runtime request that executes
// extension code MUST come from a background job: a synchronous
// UI-thread RoundTrip would leave nobody pumping host_call (it
// self-limits — the RoundTrip times out, the worker is killed, the
// parked ve_call dies with the process — but the call fails).
// Spawned WITHOUT the env vars (any non-editor client), the bridge
// is absent and ve_call keeps the v1 queued call_request + explicit
// {"ok":false,"error":"not bridged yet"} result.
#pragma once

namespace XER::VideoEditor {

// Runs the JSON-lines loop on stdin until EOF (the parent closed the
// pipe or died). `role` rides every ping/info reply — pass
// "VE.ExtensionsRuntime" so the parent proves which binary answered.
// Returns a process exit code (0 on clean EOF).
int RunExtRuntime(const char* role);

}  // namespace XER::VideoEditor
