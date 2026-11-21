// ide_project.h — headless project logic for IDEXEJSO, the VideoEditor
// extension IDE. Everything here is UI-free so the SAME code path runs
// the GUI (ide_app.cc) and the `--selftest` CLI mode (CI-able without
// a window system):
//
//   ScaffoldExtension — manifest.toml + main.js + static/index.html +
//                       scripts/ + (optional) native/hello.cc payload
//   ListProjectFiles  — the file-tree model (dirs first, sorted)
//   BuildExtension    — sibling ztzmake.exe <dir> -o <name>.xexrj
//   TestExtension     — VE.ExtensionsRuntime worker_pipe round trip:
//                       ping -> ext_load -> ext_call probe -> ext_unload
//   OpenInEditor      — deploy into <bin>/assets/extensions + launch
//                       video_editor.exe
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace XER::IdeXejso {

// One file-tree row. `rel_path` is '/'-separated relative to the
// project root; directories carry a trailing '/'.
struct FileItem {
    std::string rel_path;
    bool is_dir = false;
    int depth = 0;
};

using LogFn = std::function<void(const std::string& line)>;

// Directory the running exe lives in ("" on failure).
std::string ExeDirUtf8();
// Absolute path of a sibling exe sitting next to this binary
// ("ztzmake.exe", "VE.ExtensionsRuntime.exe", "video_editor.exe") —
// "" when absent. Sibling-only lookup, never PATH.
std::string FindSiblingExe(const char* name);

// Writes the extension skeleton under `dir` (created when missing;
// must not already hold a manifest.toml):
//   manifest.toml          — [extension] id/name/entry/ui, [files]
//                            requires, [native] sources (with_native),
//                            [pack] probe = "ve_ext_probe" — the IDE's
//                            ext_call target for Test (the extension
//                            host ignores [pack], and [native] stays
//                            all-arrays so the manifest verifies).
//   main.js                — xer_ext entry point
//   scripts/helper.js      — trivial helper (listed in requires)
//   static/index.html      — the [extension] ui page
//   native/hello.cc        — freestanding ve_* payload exporting
//                            ve_ext_entry + ve_ext_probe (with_native)
// `ext_id` is sanitized to [a-z0-9_.-] (anything else -> '-').
bool ScaffoldExtension(const std::string& dir, const std::string& ext_id,
                       const std::string& display_name, bool with_native,
                       std::string* error);

// Recursive listing of `root`: each level sorted dirs-first then by
// name. `collapsed` holds dir rel_paths (WITHOUT the trailing '/')
// whose children are pruned from the result — the tree widget keeps
// the set.
std::vector<FileItem> ListProjectFiles(
    const std::string& root,
    const std::vector<std::string>& collapsed = {});

bool ReadTextFile(const std::string& path, std::string* out,
                  std::string* error);
bool WriteTextFile(const std::string& path, const std::string& text,
                   std::string* error);

// Minimal manifest scrape — [extension] id/name/entry plus the [pack]
// probe key ("" when undeclared). Line-based like ztzmake's manifest
// reader; NOT a full TOML parser.
struct ManifestInfo {
    std::string id;
    std::string name;
    std::string entry = "main.js";
    std::string probe;
};
bool ParseManifestLite(const std::string& text, ManifestInfo* out);

// Spawns `exe` with `args`, captures merged stdout+stderr into
// `output` and the process exit into `exit_code`. false = spawn/read
// failure (a non-zero exit_code is still a successful RUN). The
// process is killed past `timeout_ms`. `on_line` (optional) receives
// each complete output line AS IT ARRIVES so the log pane streams
// instead of batching on process exit.
bool RunProcessCapture(const std::string& exe,
                       const std::vector<std::string>& args,
                       unsigned long timeout_ms, std::string* output,
                       int* exit_code,
                       const LogFn& on_line = {});

// ztzmake <dir> -o <parent-of-dir>/<dirname>.xexrj — the .xexrj lands
// as a SIBLING of the source dir so later packs never ingest their own
// output. ztzmake already self-verifies (crc + OVERFLOW) before
// exiting 0; `out_xexrj` receives the archive path. Tool output lines
// stream to `log`.
bool BuildExtension(const std::string& project_dir,
                    std::string* out_xexrj, const LogFn& log);

// VE.ExtensionsRuntime round trip over the shared WorkerPipe:
//   ping -> ext_load(path) -> ext_call(probe_fn) -> ext_unload.
// Every step + extension event (log/status/add_clip) streams to `log`.
// `probe_fn` may be empty — the conventional "ve_ext_probe" is tried;
// a "symbol not found" reply just notes "no probe exported" (the
// ext_load + ve_ext_entry already proved the JIT link). host_call
// lines on the reverse channel are answered with a stub
// {"ok":false,"error":"IDE: no project host"} so extension code that
// ve_call's reports cleanly instead of parking the worker until the
// RoundTrip timeout kills it.
// Returns the ext_load-level outcome (JS-only archives load fine —
// the runtime reports native:false and owns nothing JIT-side).
bool TestExtension(const std::string& xexrj_path,
                   const std::string& probe_fn, const LogFn& log);

// Copies the .xexrj into <exe_dir>/assets/extensions/ (the dir
// video_editor.exe scans at boot) then launches the sibling
// video_editor.exe. false with a log line when the exe is absent.
bool OpenInEditor(const std::string& xexrj_path, const LogFn& log);

// Headless `--selftest` pipeline: scaffold a scratch extension under
// %TEMP%, build it through the real ztzmake.exe sibling, verify +
// member-list the archive, then (tolerated) drive VE.ExtensionsRuntime
// when it sits beside the exe. Returns a process exit code (0 = pass).
int RunSelftest(const LogFn& log);

}  // namespace XER::IdeXejso
