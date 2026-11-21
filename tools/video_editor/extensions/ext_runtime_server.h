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
// Event envelope (the v1 ve_* bridge): every reply that ran extension
// code carries "events":[{...}] — log lines, status updates, posted
// events, and the queued add_clip/call requests the editor is
// expected to fulfill:
//   {"ext":"id","type":"log","msg":"..."}
//   {"ext":"id","type":"status","msg":"..."}
//   {"ext":"id","type":"event","event":{...}}        — ve_post
//   {"ext":"id","type":"add_clip","kind","param","start","duration"}
//   {"ext":"id","type":"call_request","name","args"} — ve_call
// v1 design note: ve_call/ve_add_clip CANNOT block on an editor
// answer — the extension call happens on the worker's single request
// thread, so a synchronous bridge would deadlock the pipe. The
// honest v1 queues them as events; a future protocol revision can
// add a second channel (the editor writes a follow-up request the
// runtime answers from a parked waiter) to fulfill them
// synchronously. log/status/project-info already work locally.
#pragma once

namespace XER::VideoEditor {

// Runs the JSON-lines loop on stdin until EOF (the parent closed the
// pipe or died). `role` rides every ping/info reply — pass
// "VE.ExtensionsRuntime" so the parent proves which binary answered.
// Returns a process exit code (0 on clean EOF).
int RunExtRuntime(const char* role);

}  // namespace XER::VideoEditor
