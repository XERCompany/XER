# ProcessTask — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Standalone Windows process/program/service auditor at
`tools/process_task/` — full enumeration (processes via Toolhelp32 +
`QueryFullProcessImageNameW`, services via SCM
`EnumServicesStatusExW`+`QueryServiceConfigW`, installed programs via
registry Uninstall keys), heuristic masquerade detection (pure rules
over plain records), pluggable SHA-256 reputation providers
(MalwareBazaar default, VirusTotal optional), `XER_UI_Core` GUI table
+ JSON/CSV export, and a headless `--scan` CLI. Defensive only.
**Architecture:** `XER_ProcessTask_Core` STATIC lib (UI-free,
globbed `*.cc` minus `main.cc`/`audit_widget.cc`) + thin
`process_task` exe. Modules: `audit_types` (records), `system_context`
(OS path sets), `process/service/program_enum` (Win32 collectors →
`AuditItem`), `file_hash`/`pe_signature` (analysis primitives),
`detection` (pure heuristic rules + scoring), `http_transport` +
`reputation` (`IHttpTransport`/`IReputationProvider` seams), 
`settings_store` (JSON settings), `audit_engine` (worker thread,
phases A–D, progress+cancel), `report_export` (JSON/CSV/table).
`audit_widget` + `main.cc` are exe-side (GUI gated `if(WIN32)`).
Tests: `XER_ProcessTask_Tests` plain-assert exe under
`tests/process_task/` — detection/export/hash tests are pure;
enum/signature/engine tests run live Win32 paths and self-skip where
capability is absent; reputation tests inject `FakeTransport` (no
live network); one optional live MB test self-skips offline.
**Tech Stack:** C++23, CMake + Ninja (preset `default`, vcpkg
`x64-windows-release`, MSVC), Win32 (tlhelp32/psapi-free,
`QueryFullProcessImageNameW`, SCM, registry, `WinVerifyTrust`),
OpenSSL EVP SHA-256, libcurl via `XER::Network::Http::SyncRequest`,
jsoncpp, `XER_UI_Core` widgets.
**Spec:** `docs/superpowers/specs/2026-11-15-process-task-design.md`
(decision: `docs/wiki/decisions/process-task.md`)
**Baseline:** branch `feature/xer-studio-1c-export` (Phase-1C
in-flight; `tools/video_editor` glob-lib pattern +
`XER_VideoEditor_Tests` harness are the templates).

## Global Constraints

- **MSVC env required for every cmake/ctest invocation** — plain
  shells fail `C1083` on standard headers. Use the repo helper:
  `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target <t>`
  and `"D:/Developers/XER3/out/vcenv.bat" ctest --test-dir out/build/default --output-on-failure`.
  (Re-configure after CMake edits: `"D:/Developers/XER3/out/vcenv.bat" cmake --preset default`.)
- Build dir `out/build/default` (Ninja preset `default`); exes land
  in `out/build/default/bin` via global `CMAKE_RUNTIME_OUTPUT_DIRECTORY`.
- **C++23**, repo style for NEW files in `tools/process_task/`:
  **4-space indent** (match `tools/video_editor/`, not the tab-using
  security_flash files), snake_case files, camelCase/PascalCase code,
  `#pragma once`, RAII (SCM/service/registry/token handles via small
  RAII wrappers — `ScmHandle` precedent at
  `security_flash_panel_widget.cc:38-46`), no exceptions in hot
  paths, contextual errors via `std::string* error_out`
  (nullptr-tolerant). `/W4` clean.
- **Namespace `XER::ProcessTask`** for ALL new code (core, GUI
  widget, tests). Sources live flat in `tools/process_task/`.
- **`src/` is READ-ONLY** — no modifications under `src/`. Allowed
  edits: `tools/process_task/**`, `tests/process_task/**`,
  `tests/CMakeLists.txt`, root `CMakeLists.txt` (one
  `add_subdirectory` line). Do NOT touch `tools/video_editor`,
  `tools/SecurityFlashService`, or any engine code.
- **`XER_ProcessTask_Core` is the ONLY new lib.** CMakeLists globs
  `*.cc` with `CONFIGURE_DEPENDS` and MUST exclude `main.cc` and
  `audit_widget.cc` from the start (video_editor plan Task 7 lesson —
  `main()`/widget code must not land in the static lib). The exe
  target is added in Task 7.
- **Win32 boundary discipline**: every Win32 string/path crosses
  UTF-16↔UTF-8 via `XER::Helper::String::WideToUtf8`/`Utf8ToWide`
  (`src/helper/string_utils.h:86-87`) and path conversion via
  `XER::Helper::Path` (`src/helper/path_utils.h`). No `char*` Win32
  calls, no ANSI paths. Compare paths ONLY through
  `NormalizePathForCompare`/`PathIsUnder` (`system_context.h`) —
  never raw string compare (case/separator/trailing-slash bugs).
- **No FFmpeg/video deps** — this tool links `XER_Network`
  (SyncRequest+OpenSSL), `XER_Helper`, `XER_Hex`, `jsoncpp_lib`,
  `wintrust`+`advapi32` (WIN32 PRIVATE). Do NOT add `XER_UI_Core` to
  the core lib — GUI stays exe-side.
- **Cross-platform stubs**: every `_WIN32`-dependent `.cc` compiles
  everywhere; non-Windows bodies return empty/false +
  `"not supported on this platform"` (security_flash pattern,
  `windows_account.h:11`). Tests self-skip via `#ifdef _WIN32` or
  capability checks — never fail on non-Windows.
- **Network isolation in tests**: reputation tests use `FakeTransport`
  (in-test `IHttpTransport`); the ONLY live-network test is the
  optional MalwareBazaar probe which self-skips when
  `SyncRequest` to `mb-api.abuse.ch` fails fast. No test may require
  internet, an API key, or a specific machine state.
- **No `src/` includes of private headers** — consume public headers
  only (`content/network/http/http_sync_request.h`,
  `helper/*.h`, `content/hex/hex.h`). Never include `<curl/*>` or
  `<openssl/*>` in tool code except `file_hash.cc`/`reputation.cc`
  internals — and prefer the `XER_Network`/`OpenSSL::Crypto` targets
  for linkage, never raw `#pragma comment(lib)`.
- Do **not** run builds during design; the plan's build commands are
  for the implementer. Do not commit on the user's behalf beyond the
  per-task commits the plan specifies.

## Review Focus

The five input classes / failure modes most likely to bite:

1. **UTF-8/UTF-16 path mangling and compare drift**: Win32 returns
   UTF-16; XER carries UTF-8 `std::string`. A missed
   `WideToUtf8`/`Utf8ToWide` round-trip produces mojibake paths that
   silently fail every `PathIsUnder` check (rules under-fire). Same
   class: `C:\Foo` vs `c:\foo\` vs `C:/Foo/` vs 8.3 short names —
   `NormalizePathForCompare` must lowercase, unify separators, strip
   trailing separators (except drive root), and `PathIsUnder` must
   compare on a separator boundary (`C:\WindowsX` is NOT under
   `C:\Windows`). `\\?\` long-path prefixes and `%SystemRoot%`-style
   env vars in service/program paths must be expanded before compare.
2. **Enumeration partial-failure tolerance**: processes exit between
   snapshot and `OpenProcess` (ERROR_INVALID_PARAMETER — skip, don't
   fail); protected processes (MsMpEng, csrss non-elevated) deny
   `OpenProcess` → `path=""` + Info finding, NOT an error; PPL
   services report `dwProcessId=0`. `EnumServicesStatusExW` needs the
   `ERROR_MORE_DATA` buffer-doubling loop (64 KiB start); SCM open
   needs `SC_MANAGER_ENUMERATE_SERVICE|CONNECT` (NOT ALL_ACCESS —
   fails unelevated). Registry Uninstall walk hits WOW6432Node view
   traps (enumerate BOTH views explicitly) and MSI bundle parents
   (subkeys without DisplayName — skip). Service `lpBinaryPathName`
   arrives quoted and arg-laden (`"C:\…\svchost.exe" -k netsvcs`)
   — quote/arg stripping must not mangle paths containing spaces.
3. **WinVerifyTrust state handling**: `WTD_STATEACTION_VERIFY` then
   ALWAYS `WTD_STATEACTION_CLOSE` (state data leaks kernel memory
   otherwise); `TRUST_E_NOSIGNATURE` ⇒ `Unsigned`, other trust
   errors ⇒ `SignedUntrusted`, file/crypto errors ⇒ `Error` — the
   three-way mapping is the whole game (catalog-signed system DLLs
   legitimately land `SignedUntrusted`-adjacent states; UI copy must
   not call it "malicious"). Signer CN extraction via
   `WTHelperGetProvSignerFromChain` is best-effort — null state data
   or missing cert chain must degrade to empty `signer`, never crash.
   Non-PE inputs (the `.txt` test) must return cleanly.
4. **Reputation provider robustness**: every failure mode — DNS,
   timeout, HTTP 500, malformed JSON, missing fields — must produce
   `verdict=Error` + detail, never throw/hang/crash. `SyncRequest`
   already bounds the exchange (`timeout_ms`, 15 s connect cap) —
   providers pass ~20 s. VirusTotal quota: first 401/429 sets the
   provider's stop flag (`ShouldStopBatch`) so the engine drains the
   queue instead of burning the daily quota. Hash dedupe is by
   DIGEST not path (same file via two paths = one lookup; two
   different-path same-content files also dedupe — correct).
   `SuspiciousOnly` scope reads `suspicion_score` computed by the
   interim `FinalizeSuspicion` in Phase B — ordering bug here
   silently looks up everything or nothing.
5. **Cancellation and thread lifetime**: `Cancel()` must be honored
   between Phase-B items AND between reputation HTTP calls (check
   the atomic before each `Lookup`); worker pool threads must also
   observe it. `~AuditEngine` joins the worker — no detached thread
   touching a destroyed engine. GUI `Tick` polls `Progress()`/
   `Phase()` which lock a mutex; `Result()` is only valid after
   `Wait()`/terminal phase — document and enforce by leaving it
   empty until finalize. A cancelled scan keeps partial items but
   `success=false`; export buttons stay disabled until a successful
   scan (never export a torn result silently — the JSON does carry
   `cancelled` for honesty if a cancelled result IS exported via
   CLI, see Task 6).

---

## Task 1: Scaffold — CMake, `audit_types.h`, `system_context`, test infra

Foundation everything else builds on: the core lib target, the record
types, OS path context, and the test harness. Independently
committable; produces a buildable (if empty-ish) lib + green tests.

**Files:**
- Create: `tools/process_task/CMakeLists.txt` (core lib ONLY — exe
  lands in Task 7)
- Create: `tools/process_task/audit_types.h`
- Create: `tools/process_task/system_context.h`,
  `tools/process_task/system_context.cc`
- Create: `tests/process_task/process_task_test_util.h`,
  `tests/process_task/process_task_test_main.cc`,
  `tests/process_task/process_task_system_context_tests.cc`
- Modify: `CMakeLists.txt` (root — add `add_subdirectory(tools/process_task)` after the video_editor line ~476)
- Modify: `tests/CMakeLists.txt` (register `XER_ProcessTask_Tests`)

**Interfaces:**
- Produces (`tools/process_task/audit_types.h`, namespace
  `XER::ProcessTask`; includes `<cstdint> <string> <vector>` +
  `#pragma once`) — EXACTLY the spec's data model:

```cpp
enum class ItemKind : std::uint8_t { Process, Program, Service };
enum class Severity : std::uint8_t { Info, Low, Medium, High, Critical };
enum class SignatureStatus : std::uint8_t {
    NotChecked, Trusted, SignedUntrusted, Unsigned, Error };
enum class ReputationVerdict : std::uint8_t {
    NotQueried, Clean, Suspicious, Malicious, Error };
enum class ReputationScope : std::uint8_t {
    Auto, SuspiciousOnly, AllCandidates };
enum class ScanPhase : std::uint8_t {
    Idle, Enumerating, Analyzing, Reputation, Done, Failed, Cancelled };

struct Finding {
    Severity severity = Severity::Info;
    std::string rule;      // stable id, e.g. "PT-SYSNAME-PATH"
    std::string message;
};
struct ReputationResult {
    bool queried = false;
    std::string provider;
    ReputationVerdict verdict = ReputationVerdict::NotQueried;
    int malicious = 0, suspicious = 0, undetected = 0, harmless = 0;
    std::string detail;
};
struct AuditItem { /* all fields per spec data model — kind, name,
    display_name, pid, ppid, path, command_line, user,
    start_time_utc, service_name, binary_path_raw, start_type, state,
    service_dll, publisher, version, install_location,
    uninstall_string, install_date, signature, signer, hashed,
    sha256, reputation, findings, suspicion_score, worst_severity */ };
struct ScanOptions { /* check_signatures, hash_files, use_reputation,
    reputation_scope, provider_override, reputation_api_key — all
    defaults per spec */ };
struct ScanProgress { ScanPhase phase; int items_done, items_total;
    std::string stage; };
struct ScanResult { bool success, cancelled; std::string error;
    std::vector<AuditItem> items; std::string started_utc,
    finished_utc; ScanOptions options_used; std::string hostname; };
```

- Produces (`system_context.h`, namespace `XER::ProcessTask`):

```cpp
struct SystemContext {
    std::string system_root, system32_dir, syswow64_dir,
                program_data_dir, temp_dir, local_appdata_dir,
                appdata_dir, user_profile_dir, public_dir,
                program_files_dir, program_files_x86_dir;
    int own_pid = 0;
    std::vector<std::string> user_writable_roots;  // normalized
    std::vector<std::string> removable_roots;      // normalized
    bool IsUnderUserWritable(const std::string& normalized_path) const;
    bool IsUnderSystemDirs(const std::string& normalized_path) const;
};

// Lowercase, '/'->'\\', collapse ".\"/"..\" best-effort, strip trailing
// separators except drive root, expand nothing (enums expand env vars
// at collection time). "" in → "" out.
std::string NormalizePathForCompare(std::string_view utf8_path);
// Separator-boundary prefix test on ALREADY-normalized inputs:
// PathIsUnder("c:\\windows", "c:\\windows\\system32") == true;
// PathIsUnder("c:\\windows", "c:\\windowsx") == false;
// PathIsUnder("c:\\", "c:\\foo") == true. Empty root/path → false
// (except identical non-empty → true).
bool PathIsUnder(const std::string& normalized_root,
                 const std::string& normalized_path);
SystemContext CollectSystemContext();   // real on _WIN32, empty-ish else
```

- Consumes: `helper/string_utils.h` (`ToLowerAscii`,
  `WideToUtf8`, `Utf8ToWide`), `helper/env_utils.h` (`GetEnv`),
  `helper/path_utils.h` if needed for UTF-8→native path calls.
- Produces: consumed by EVERY later task (types) and Task 4
  (context + helpers).

- [ ] **Step 1: Write the failing test**

`tests/process_task/process_task_test_util.h` — copy the
`video_editor_test_util.h` pattern verbatim, renamed:

```cpp
#pragma once
#include <cstdio>
#include <filesystem>
namespace XER::ProcessTask::Test {
inline int g_failures = 0;
inline void Check(bool c, const char* e, const char* f, int l) {
    if (c) return; ++g_failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", f, l, e);
}
inline int Failures() { return g_failures; }
inline std::filesystem::path TempDir() {
    const auto d = std::filesystem::temp_directory_path() /
                   "xer_process_task_tests";
    std::filesystem::create_directories(d); return d;
}
}  // namespace XER::ProcessTask::Test
#define XER_PT_CHECK(cond) \
    XER::ProcessTask::Test::Check(static_cast<bool>(cond), #cond, \
                                  __FILE__, __LINE__)
```

`process_task_test_main.cc` — same shape as
`video_editor_test_main.cc`: forward-declare suite runners, call
them, print summary, return clamped failure count.

`process_task_system_context_tests.cc` — expose
`int XER_ProcessTask_RunSystemContextTests();` Cases:

```cpp
// NormalizePathForCompare (pure, runs everywhere):
//   "C:\\Foo\\BAR\\" == "c:\\foo\\bar" ; "C:/Foo" == "c:\\foo" ;
//   "C:\\\\Windows\\\\" handled ; "" -> "" ; "\\\\server\\share\\" keeps UNC.
// PathIsUnder boundary traps:
//   ("c:\\windows","c:\\windows\\system32") true;
//   ("c:\\windows","c:\\windowsx\\evil") false;   // THE separator bug
//   ("c:\\","c:\\anything") true;
//   ("","c:\\x") false; ("c:\\x","") false;
// CollectSystemContext (#ifdef _WIN32 only — whole section guarded):
//   system_root non-empty, ends_with "windows" (case-insens);
//   system32_dir starts_with system_root; own_pid == GetCurrentProcessId();
//   user_writable_roots non-empty and NONE equals system32_dir;
//   temp_dir non-empty.
```

- [ ] **Step 2: Write CMake + wire, build — verify RED**

`tools/process_task/CMakeLists.txt` (lib only — exe in Task 7):

```cmake
# ProcessTask core — headless enumeration/analysis/detection/reputation/
# engine/export modules. UI-free; consumed by the process_task exe and
# XER_ProcessTask_Tests. CONFIGURE_DEPENDS so new .cc files need no
# manual re-configure (flag order matters on CMake 4.4.x — see
# tools/video_editor/CMakeLists.txt).
file(GLOB PROCESS_TASK_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
# main.cc / audit_widget.cc are exe-side — never in the static lib.
list(FILTER PROCESS_TASK_CORE_SOURCES EXCLUDE REGEX "/(main|audit_widget)\\.cc$")

add_library(XER_ProcessTask_Core STATIC ${PROCESS_TASK_CORE_SOURCES})
target_include_directories(XER_ProcessTask_Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_link_libraries(XER_ProcessTask_Core PUBLIC
    XER_Network      # SyncRequest transport + OpenSSL::Crypto (transitive)
    XER_Helper       # string/path/env/jsoncpp helpers
    XER_Hex          # header-only hex encode
    jsoncpp_lib
)
target_compile_features(XER_ProcessTask_Core PUBLIC cxx_std_23)
if(WIN32)
    target_link_libraries(XER_ProcessTask_Core PRIVATE wintrust advapi32)
endif()
if(MSVC)
    target_compile_options(XER_ProcessTask_Core PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ProcessTask_Core PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
```

Root `CMakeLists.txt` after `add_subdirectory(tools/video_editor)`:

```cmake
# ProcessTask — standalone process/program/service auditor (tools/).
add_subdirectory(tools/process_task)
```

`tests/CMakeLists.txt` after the `XER_VideoEditor_Tests` block:

```cmake
add_executable(XER_ProcessTask_Tests
    process_task/process_task_test_util.h
    process_task/process_task_test_main.cc
    process_task/process_task_system_context_tests.cc
)
target_link_libraries(XER_ProcessTask_Tests PRIVATE XER_ProcessTask_Core)
target_compile_features(XER_ProcessTask_Tests PRIVATE cxx_std_23)
add_test(NAME XER_ProcessTask_Tests COMMAND XER_ProcessTask_Tests)
```

Run: `"D:/Developers/XER3/out/vcenv.bat" cmake --preset default` then
`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_ProcessTask_Tests`.
Expected RED: compile error — `audit_types.h`/`system_context.h` not
found (test includes them; lib has no sources yet — CMake accepts an
empty-source static lib but the TEST TU fails first).

- [ ] **Step 3: Implement `audit_types.h` + `system_context.{h,cc}`**

`audit_types.h`: verbatim the spec data model (enums, `Finding`,
`ReputationResult`, `AuditItem`, `ScanOptions`, `ScanProgress`,
`ScanResult`) — every field documented per spec. `AuditItem` gets a
`kind` default `ItemKind::Process`.

`system_context.cc`:
- `NormalizePathForCompare`: operate on a `std::string` copy —
  `XER::Helper::String::ToLowerAscii`, replace `'/'`→`'\\'`, collapse
  `\\` runs (keep UNC `\\\\` prefix — detect leading `\\\\` and
  preserve exactly two), resolve `.`/`..` segments with a vector
  stack (drive-letter and UNC roots preserved), strip trailing `\\`
  unless the path IS a root (`c:\`, `\\server\share\`). Empty→empty.
- `PathIsUnder`: normalize defensively is the CALLER's job — assert
  contract in comment; implement: `path == root` → true;
  `path.size() > root.size()` && `path.compare(0,root.size(),root)==0`
  && `path[root.size()]=='\\'` → true; else false. (Root `c:\`
  already ends with `\\` so the boundary char check still works:
  `c:\foo`[3]=='f' — handle by ensuring root ends with `\\` OR
  comparing `root` without its trailing sep + requiring next char
  `\\`; simplest correct impl: strip root's trailing `\\` for the
  prefix compare, then require `path[root_len]=='\\'`.) Write it
  carefully — this is Review Focus #1.
- `CollectSystemContext` (`#ifdef _WIN32` real body):
  `GetSystemWindowsDirectoryW`/`GetSystemDirectoryW` →
  `system_root`/`system32_dir`; `syswow64_dir` =
  `system_root + "\\SysWOW64"` if it exists (`GetFileAttributesW`);
  `program_data_dir` = `%ProgramData%` env (fallback
  `C:\ProgramData`); temp/localappdata/appdata/userprofile/public/
  programfiles/programfilesx86 from env (`GetEnv` + known var names:
  `TEMP`/`TMP`, `LOCALAPPDATA`, `APPDATA`, `USERPROFILE`, `PUBLIC`,
  `ProgramFiles`, `ProgramFiles(x86)` — env names with parens need
  the literal `"ProgramFiles(x86)"`); `own_pid` =
  `GetCurrentProcessId()`; `user_writable_roots` = normalize each of
  {temp, localappdata, appdata, userprofile+"\\Downloads",
  public} that is non-empty; `removable_roots` = for each drive letter
  A–Z `GetDriveTypeW` ∈ {DRIVE_REMOVABLE, DRIVE_CDROM} → `"x:\\"`
  normalized. Dedupe + drop empties. Non-Windows: return default
  `SystemContext{}` with `own_pid=-1`.
- `IsUnderUserWritable`/`IsUnderSystemDirs`: loop `PathIsUnder` over
  the vectors; `IsUnderSystemDirs` = system32||syswow64 only.

- [ ] **Step 4: Build + run — verify GREEN**

Build `XER_ProcessTask_Tests`, run exe (or `ctest -R
XER_ProcessTask`). Expected: all checks pass — normalize/path tests
everywhere; context tests on Windows.

- [ ] **Step 5: Commit**

```bash
git add tools/process_task/CMakeLists.txt tools/process_task/audit_types.h \
        tools/process_task/system_context.{h,cc} \
        tests/process_task/ tests/CMakeLists.txt CMakeLists.txt
git commit -m "Add ProcessTask scaffold: types, system context, test harness"
```

---

## Task 2: Windows enumeration — processes, services, installed programs

The three collectors producing `AuditItem`s. All UI-free, all in the
core lib, all with `_WIN32` real bodies + non-Windows stubs.

**Files:**
- Create: `tools/process_task/process_enum.h`,
  `tools/process_task/process_enum.cc`
- Create: `tools/process_task/service_enum.h`,
  `tools/process_task/service_enum.cc`
- Create: `tools/process_task/program_enum.h`,
  `tools/process_task/program_enum.cc`
- Create: `tests/process_task/process_task_enum_tests.cc`

**Interfaces:**
- Produces (all `namespace XER::ProcessTask`, headers include only
  `audit_types.h` + `<string> <vector>`):

```cpp
// process_enum.h — Toolhelp32 snapshot + per-pid image path/times.
// kind=Process. path="" + is_running semantics per spec when
// OpenProcess fails. ppid from th32ParentProcessID.
std::vector<AuditItem> EnumerateProcesses(std::string* error_out = nullptr);

// service_enum.h — SCM enum + per-service config. kind=Service.
// state/start_type strings per spec; service_dll filled for
// svchost-hosted services; pid = dwProcessId (0 when stopped/driver).
std::vector<AuditItem> EnumerateServices(std::string* error_out = nullptr);

// program_enum.h — 4 registry Uninstall views, deduped.
// kind=Program; path=InstallLocation (may be ""); skips
// SystemComponent=1 and DisplayName-less bundle parents.
std::vector<AuditItem> EnumerateInstalledPrograms(
    std::string* error_out = nullptr);
```

- Consumes: `audit_types.h`, `system_context.h`
  (`NormalizePathForCompare` for stored paths' compare-readiness is
  NOT needed — store raw UTF-8; detection normalizes),
  `helper/string_utils.h` (`WideToUtf8`, `Utf8ToWide`,
  `ToLowerAscii`, `StartsWith`), `windows.h` (guarded `#ifdef _WIN32`).
- Produces: consumed by `AuditEngine` (Task 6) and enum tests.

- [ ] **Step 1: Write the failing enum tests**

`process_task_enum_tests.cc` — expose
`int XER_ProcessTask_RunEnumTests();`. Register the runner in
`process_task_test_main.cc`. Whole live section guarded:

```cpp
#ifdef _WIN32
// include <windows.h> for GetCurrentProcessId
#endif

int XER_ProcessTask_RunEnumTests() {
#ifdef _WIN32
    std::string err;
    // --- processes ---
    const auto procs = EnumerateProcesses(&err);
    XER_PT_CHECK(!procs.empty());                 // any Windows host
    XER_PT_CHECK(procs.size() < 100000);
    bool saw_self = false;
    for (const auto& p : procs) {
        XER_PT_CHECK(p.kind == ItemKind::Process);
        XER_PT_CHECK(p.pid > 0 || p.pid == 0);    // Idle=0 allowed
        if (p.pid == GetCurrentProcessId()) {
            saw_self = true;
            XER_PT_CHECK(!p.path.empty());       // our own exe resolves
            XER_PT_CHECK(p.path.find(".exe") != std::string::npos);
        }
    }
    XER_PT_CHECK(saw_self);                        // we exist!

    // --- services ---
    const auto svcs = EnumerateServices(&err);
    XER_PT_CHECK(!svcs.empty());                   // Windows has services
    int running = 0;
    for (const auto& s : svcs) {
        XER_PT_CHECK(s.kind == ItemKind::Service);
        XER_PT_CHECK(!s.service_name.empty());
        XER_PT_CHECK(!s.state.empty());
        XER_PT_CHECK(!s.start_type.empty());
        XER_PT_CHECK(s.pid >= 0);
        if (s.state == "running") ++running;
    }
    XER_PT_CHECK(running > 0);                      // something runs

    // --- programs ---
    const auto progs = EnumerateInstalledPrograms(&err);
    // May legitimately be small/empty on stripped systems — assert
    // only structural sanity when non-empty.
    for (const auto& g : progs) {
        XER_PT_CHECK(g.kind == ItemKind::Program);
        XER_PT_CHECK(!g.name.empty());             // DisplayName required
    }
    XER_PT_CHECK(progs.size() < 100000);
#else
    std::string err;
    XER_PT_CHECK(EnumerateProcesses(&err).empty());
    XER_PT_CHECK(EnumerateServices(&err).empty());
    XER_PT_CHECK(EnumerateInstalledPrograms(&err).empty());
    XER_PT_CHECK(!err.empty());                    // "not supported"
#endif
    return XER::ProcessTask::Test::Failures();
}
```

Runner convention (match `video_editor_test_main.cc` exactly): each
suite .cc exposes `int XER_ProcessTask_Run<Suite>Tests();`; the
shared `g_failures` counter in `process_task_test_util.h` does the
real work — runners may return the count but `test_main` ignores
return values and reports `Test::Failures()` at the end. Add the
runner forward-declaration + call to `process_task_test_main.cc` and
the .cc to `tests/CMakeLists.txt` sources.

- [ ] **Step 2: Build — verify RED**

Add the three suite `.cc` includes won't compile (headers missing).
Build `XER_ProcessTask_Tests` → expected: `process_enum.h` not found.

- [ ] **Step 3: Implement `process_enum.cc`**

`#ifdef _WIN32` body:
- `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)`; INVALID →
  error_out "CreateToolhelp32Snapshot failed: <gle>". RAII close.
- `PROCESSENTRY32W` loop (`Process32FirstW/NextW`): pid=
  `th32ProcessID`, ppid=`th32ParentProcessID`, name=WideToUtf8
  (`szExeFile` is WCHAR on the W structs — use the `W` variants).
- Per pid: `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
  pid)` — on failure leave `path=""` (access denied/ exited — the
  Info finding is detection's job, Task 4). On success:
  `QueryFullProcessImageNameW(h, 0, buf, &size)` → WideToUtf8 →
  `path`; `GetProcessTimes` → creation `FILETIME` → UTC ISO-8601
  `start_time_utc` (small local helper `FileTimeToIsoUtc` —
  `FileTimeToSystemTime` + `SystemTimeToTls`? NO — use
  `SystemTimeToVariantTime`? simplest correct: convert FILETIME→
  `std::chrono::system_clock::time_point` via the 11644473600s epoch
  offset, then `std::chrono::current_zone()`/`std::format`? C++20
  `std::chrono::time_zone` support in MSVC STL is present but heavy —
  simplest dependency-free: `gmtime_s` on `time_t` and
  `snprintf("%Y-%m-%dT%H:%M:%SZ")`. ~15 lines, deterministic UTC.
  Document choice in comment.) Close handle each iteration (RAII
  `unique_handle` or explicit CloseHandle — file already long;
  prefer a tiny local RAII struct).
- `is_running` isn't a model field — skip; `kind=Process` on every
  item; `name` = image name (may be "" for pid 0/4 — leave name
  empty, detection skips them anyway); skip NOTHING (record all,
  including Idle/System — detection filters).
- Non-`_WIN32`: `error_out="process enumeration is Windows-only"`,
  return {}.

- [ ] **Step 4: Implement `service_enum.cc`**

`#ifdef _WIN32` body:
- RAII `ScmHandle`/`ServiceHandle` (copy the panel-widget pattern).
- `OpenSCManagerW(nullptr,nullptr, SC_MANAGER_ENUMERATE_SERVICE |
  SC_MANAGER_CONNECT)` → null: error_out "OpenSCManager failed:
  <gle>" (access denied is EXPECTED unelevated? — ENUMERATE_SERVICE
  works unelevated for read; still handle failure).
- `EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
  SERVICE_STATE_ALL, buf, size, &needed, &returned, nullptr, nullptr)`
  — buffer loop: start 64 KiB `std::vector<uint8_t>`, on
  `ERROR_MORE_DATA` double (cap 4 MiB → error). Cast to
  `ENUM_SERVICE_STATUS_PROCESSW*`.
- Per entry: `name`=WideToUtf8(`lpServiceName`),
  `display_name`=WideToUtf8(`lpDisplayName`) (fallback name when
  empty), `service_name`=same as name, `state`=map
  `dwCurrentState`→string (running/stopped/start_pending/
  stop_pending/continue_pending/pause_pending/paused — small switch,
  default "unknown"), `pid`=`dwProcessId`, `start_type` filled AFTER
  config query.
- `OpenServiceW(scm, name_w, SERVICE_QUERY_CONFIG)` per service →
  `QueryServiceConfigW` into growable `QUERY_SERVICE_CONFIGW` (loop
  on `ERROR_INSUFFICIENT_BUFFER`, cap 64 KiB) →
  `binary_path_raw`=WideToUtf8(`lpBinaryPathName`) (may be "" for
  some drivers); `start_type` map `dwStartType`:
  SERVICE_BOOT_START→"boot", SYSTEM_START→"system",
  AUTO_START→"auto", DEMAND_START→"manual", DISABLED→"disabled",
  else "unknown".
- **Binary path normalization** → `path`: copy `binary_path_raw`;
  `ExpandEnvironmentStringsW` (UTF-16 in/out — convert raw→wide,
  expand, back to UTF-8); strip surrounding `"`; if unquoted take
  substring up to first whitespace BUT respect that args follow —
  rule: if starts with `"` → token = inside quotes; else token = up
  to first space/tab. Then if basename(lower) == `svchost.exe`,
  record full token INCLUDING args? NO — `path` = image path only;
  args are dropped in v1 (command_line stays empty per spec).
  `\\SystemRoot\` / `\\??\` prefixes: replace `\\SystemRoot\` with
  `system_root`+`\\` (case-insensitive, via `NormalizePathForCompare`
  awareness — do the replace on the WIDE string before UTF-8
  conversion using the known system_root wide form; `\\??\` strip
  prefix). Best-effort — document.
- **service_dll**: only when normalized basename(lower) of `path` ==
  `svchost.exe` → `RegOpenKeyExW(HKEY_LOCAL_MACHINE,
  L"SYSTEM\\CurrentControlSet\\Services\\<name_w>\\Parameters",
  0, KEY_QUERY_VALUE)` → `RegQueryValueExW("ServiceDll")` →
  expand env → UTF-8 → `service_dll`. Failure → leave empty (never
  fatal). 32-bit services: `CurrentControlSet\Services` is shared —
  no WOW6432Node redirect for this key (verified: Services key is not
  reflected); read once.
- Non-`_WIN32`: stub + "services enumeration is Windows-only".

- [ ] **Step 5: Implement `program_enum.cc`**

`#ifdef _WIN32` body:
- Four roots (HKEY_LOCAL_MACHINE 64-bit view is default for x64
  process — NO `KEY_WOW64_64KEY` flag needed but ADD
  `KEY_WOW64_64KEY` explicitly for clarity/robustness; WOW6432Node
  path spelled out; HKCU both views):
  - `SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall`
  - `SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall`
  (HKLM only — HKCU has no WOW6432Node Uninstall in practice; still
  harmless to try both, spec says 4 views: HKLM×2 + HKCU×2 where
  HKCU's second is also WOW6432Node — implement all four
  uniformly.)
- RAII `RegHandle`. Per root: `RegOpenKeyExW` (missing → skip
  silently), `RegEnumKeyExW` loop → subkey name → open subkey
  `KEY_QUERY_VALUE` → read values: `DisplayName` (REG_SZ/EXPAND_SZ —
  required, skip subkey if absent/empty), `DisplayVersion`,
  `Publisher`, `InstallLocation`, `UninstallString`, `InstallDate`,
  `EstimatedSize` (skip — not in model? EstimatedSize IS in spec?
  — check spec: program fields are publisher/version/
  install_location/uninstall_string/install_date — NO
  estimated_size; skip reading it), `SystemComponent` (REG_DWORD==1
  → skip entry).
- REG_EXPAND_SZ values: expand via `ExpandEnvironmentStringsW`
  before UTF-8 (InstallLocation/UninstallString commonly contain
  `%ProgramFiles%`).
- Build `AuditItem`: `kind=Program`, `name`=DisplayName,
  `display_name`=DisplayName, `publisher`, `version`=DisplayVersion,
  `install_location`=InstallLocation, `uninstall_string`=
  UninstallString, `install_date`, `path`=InstallLocation ("" if
  absent).
- **Dedupe**: key = lowercase(`uninstall_string`)+"|"+
  lowercase(`install_location`); if both empty use lowercase(name).
  `std::unordered_set<std::string>` seen; first wins (HKLM before
  HKCU — order the root list so machine-wide entries win).
- Non-`_WIN32`: stub + "installed-program enumeration is
  Windows-only".

- [ ] **Step 6: Build + run — verify GREEN**

Build `XER_ProcessTask_Tests`, run. Expected: enum tests pass on
Windows (processes contain self; services non-empty with running>0;
programs structurally sane). If `progs` is empty on the dev machine
that's acceptable per test design — but SANITY-CHECK manually that
real programs appear (run a quick debug print or trust the smoke in
Task 7's `--scan`).

- [ ] **Step 7: Commit**

```bash
git add tools/process_task/process_enum.{h,cc} \
        tools/process_task/service_enum.{h,cc} \
        tools/process_task/program_enum.{h,cc} \
        tests/process_task/process_task_enum_tests.cc \
        tests/process_task/process_task_test_main.cc \
        tests/CMakeLists.txt
git commit -m "Add ProcessTask Windows enumeration: processes, services, programs"
```

---

## Task 3: Analysis primitives — `file_hash` (SHA-256) + `pe_signature` (WinVerifyTrust)

Two small, heavily-tested primitives every later phase consumes.

**Files:**
- Create: `tools/process_task/file_hash.h`,
  `tools/process_task/file_hash.cc`
- Create: `tools/process_task/pe_signature.h`,
  `tools/process_task/pe_signature.cc`
- Create: `tests/process_task/process_task_hash_tests.cc`,
  `tests/process_task/process_task_signature_tests.cc`

**Interfaces:**
- Produces:

```cpp
// file_hash.h
// SHA-256 of file bytes -> 64-char lowercase hex. Streams in 1 MiB
// chunks via OpenSSL EVP. false + error_out on open/read failure.
// Never throws; utf8_path converted via XER::Helper::Path boundary.
bool Sha256FileHex(const std::string& utf8_path,
                   std::string* hex_out,
                   std::string* error_out = nullptr);

// pe_signature.h
struct SignatureInfo {
    SignatureStatus status = SignatureStatus::NotChecked;
    std::string signer;   // cert subject CN, best-effort ""
    std::string detail;   // trust error / note for UI+export
};
// WinVerifyTrust GENERIC_VERIFY_V2, WTD_UI_NONE, WTD_REVOKE_NONE.
// Mapping: ERROR_SUCCESS->Trusted; TRUST_E_NOSIGNATURE->Unsigned;
// file/crypto-not-found errors->Error; other trust failures->
// SignedUntrusted. ALWAYS WTD_STATEACTION_CLOSE. Never throws.
SignatureInfo CheckAuthenticode(const std::string& utf8_path);
```

- Consumes: `audit_types.h` (SignatureStatus), `helper/string_utils.h`
  (`Utf8ToWide`, `WideToUtf8`), `helper/path_utils.h`,
  `content/hex/hex.h` (`XER::Content::Hex::Encode`), OpenSSL EVP
  (`<openssl/evp.h>` — PRIVATE to this TU), `<wintrust.h>
  <softpub.h> <wincrypt.h>` (WIN32 only).
- Produces: consumed by `AuditEngine` Phase B (Task 6) — fields map
  onto `AuditItem::signature`/`signer`; `CheckAuthenticode` detail →
  export/UI only.

- [ ] **Step 1: Write the failing tests**

`process_task_hash_tests.cc` — `XER_ProcessTask_RunHashTests()`:

```cpp
// Known-answer vectors (write bytes to TempDir files):
//   ""      -> e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
//   "abc"   -> ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
//   1 MiB of 0xAB -> assert 64 lowercase hex chars + re-hash equal
// Missing file -> false + non-empty error_out.
// Directory path -> false (open fails or reads nothing — assert false).
```

`process_task_signature_tests.cc` —
`XER_ProcessTask_RunSignatureTests()`:

```cpp
// Temp .txt with text -> status != Trusted (Unsigned or Error both
//   acceptable — assert != Trusted; record which in a comment-friendly
//   way: CHECK(info.status == Unsigned || info.status == Error)).
// Missing file -> status == Error (or Unsigned — pin ONE: spec says
//   file/crypto errors -> Error; CHECK == Error).
// kernel32.dll: if exists at system32 (GetSystemDirectoryW +
//   "\\kernel32.dll", #ifdef _WIN32): CHECK(status == Trusted) and
//   signer contains "Microsoft" (case-insens). Retail Windows always
//   catalog/embedded-signs it — this is the positive-path proof.
//   Guard the whole check behind file-exists so odd installs skip.
```

Register both runners in `process_task_test_main.cc` +
`tests/CMakeLists.txt` sources.

- [ ] **Step 2: Build — verify RED** (headers missing → compile error)

- [ ] **Step 3: Implement `file_hash.cc`**

- Path → `XER::Helper::Path` native form for `std::ifstream` (or
  `_wfopen` via `Utf8ToWide` — pick whichever `path_utils.h`
  exposes; read its header first, use its canonical helper).
- `std::ifstream` binary; `EVP_MD_CTX_new` +
  `EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr)`; loop
  `read(1MiB)` → `EVP_DigestUpdate`; `EVP_DigestFinal_ex` → 32 bytes
  → `XER::Content::Hex::Encode` → `*hex_out`. RAII-free manual
  `EVP_MD_CTX_free` (or `std::unique_ptr` with custom deleter —
  match repo style; OpenSSL 3 has `EVP_MD_CTX_free` only).
- Any open/read/digest failure → `error_out` descriptive + false;
  empty file → valid hash of empty input (e3b0c442…).
- Non-Windows: same code works (ifstream+EVP are portable) — NO
  `#ifdef` needed; it compiles everywhere. Document that.

- [ ] **Step 4: Implement `pe_signature.cc`**

`#ifdef _WIN32` body (~120 lines):
- `WINTRUST_FILE_INFO fi{}`: `cbStruct=sizeof`, `pcwszFilePath` =
  `Utf8ToWide(path).c_str()` (keep the wstring ALIVE in a local —
  WinVerifyTrust reads it synchronously).
- `GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;`
- `WINTRUST_DATA wd{}`: `cbStruct`, `dwUIChoice=WTD_UI_NONE`,
  `fdwRevocationChecks=WTD_REVOKE_NONE`, `dwUnionChoice=
  WTD_CHOICE_FILE`, `pFile=&fi`, `dwStateAction=
  WTD_STATEACTION_VERIFY`.
- `const LONG rc = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE)
  /*or nullptr — use nullptr, no UI anyway*/, &action, &wd);`
- Map rc: `ERROR_SUCCESS`(0)→Trusted; `TRUST_E_NOSIGNATURE`
  (0x800B0100)→Unsigned; `CRYPT_E_FILE_ERROR`, `ERROR_FILE_NOT_FOUND`,
  `TRUST_E_SUBJECT_FORM_UNKNOWN`, `CERT_E_CHAINING`? — NO: keep the
  mapping tight per spec: file-missing/not-PE (`CRYPT_E_FILE_ERROR`,
  `ERROR_FILE_NOT_FOUND`, `ERROR_PATH_NOT_FOUND`, `TRUST_E_FILE_ERROR`?)
  →Error; EVERYTHING else nonzero →SignedUntrusted. Put the exact
  HRESULT constants with comments; when unsure between Unsigned and
  Error for an unknown code → SignedUntrusted is the safer label
  (it means "we couldn't prove clean").
- Signer extraction (best-effort, runs whenever `wd.hWVTStateData`
  non-null): `WTHelperProvDataFromStateData` →
  `WTHelperGetProvSignerFromChain(pd,0,FALSE,0)` →
  `signer->pasCertChain->rgpChain[0]->rgpElement[0]->pCertContext` →
  `CertGetNameStringW(..., CERT_NAME_SIMPLE_DISPLAY_TYPE, ...)` →
  WideToUtf8 → `signer`. Every pointer null-checked.
- `wd.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(...,&wd)`
  again to release — ALWAYS, both paths (use a scope guard or
  careful ordering; document).
- `detail` = `"WinVerifyTrust rc=0x%08lX"` formatted + on
  SignedUntrusted append the hr hex (UI shows it verbatim).
- Non-`_WIN32`: `status=Error, detail="not supported on this
  platform"`.

- [ ] **Step 5: Build + run — verify GREEN**

Known vectors pass; `.txt`→non-Trusted; kernel32→Trusted+Microsoft
(if present). If kernel32 check fails on the dev box, investigate
before proceeding (likely a mapping bug, not the test).

- [ ] **Step 6: Commit**

```bash
git add tools/process_task/file_hash.{h,cc} tools/process_task/pe_signature.{h,cc} \
        tests/process_task/process_task_hash_tests.cc \
        tests/process_task/process_task_signature_tests.cc \
        tests/process_task/process_task_test_main.cc tests/CMakeLists.txt
git commit -m "Add ProcessTask SHA-256 file hashing and Authenticode checks"
```

---

## Task 4: Detection engine — `detection.{h,cc}` (pure rules)

The heart of the feature: every heuristic as a pure function over
`AuditItem`+`SystemContext`. Fully unit-testable, no Win32.

**Files:**
- Create: `tools/process_task/detection.h`,
  `tools/process_task/detection.cc`
- Create: `tests/process_task/process_task_detection_tests.cc`

**Interfaces:**
- Produces:

```cpp
namespace XER::ProcessTask {

// Appends heuristic findings to item->findings (does NOT clear
// existing — reputation findings may already be present in theory;
// engine order is heuristics-then-reputation anyway). Pure: no I/O,
// no Win32, no clock.
void EvaluateItemHeuristics(AuditItem* item,
                            const SystemContext& ctx);

// Maps a reputation result onto PT-REP-* findings. Malicious→
// Critical "PT-REP-MALICIOUS"; Suspicious→High "PT-REP-SUSPICIOUS";
// Error→Info "PT-REP-ERROR"; Clean/NotQueried→nothing.
void AppendReputationFinding(AuditItem* item,
                             const ReputationResult& rep);

// score = min(100, Σ severityWeight) where weights are
// Info=0 Low=5 Medium=15 High=30 Critical=50 (weight is FULLY
// determined by severity — every rule's weight equals its severity's
// canonical value; verified in spec rule table). worst_severity =
// max severity present (Info when none). Idempotent — recomputes
// from scratch each call.
void FinalizeSuspicion(AuditItem* item);

// Convenience = EvaluateItemHeuristics + FinalizeSuspicion.
void EvaluateAndFinalize(AuditItem* item, const SystemContext& ctx);

}  // namespace XER::ProcessTask
```

- Consumes: `audit_types.h`, `system_context.h`,
  `helper/string_utils.h` (`ToLowerAscii`, `StartsWith`, `EndsWith`).
- Produces: consumed by `AuditEngine` (Task 6) — Phase B calls
  `EvaluateAndFinalize`; Phase C calls `AppendReputationFinding` +
  `FinalizeSuspicion`; scope filter reads `suspicion_score`.

- [ ] **Step 1: Write the failing detection tests**

`process_task_detection_tests.cc` —
`XER_ProcessTask_RunDetectionTests()`. Build a fixture
`SystemContext` BY HAND (don't call CollectSystemContext — tests
must be deterministic):

```cpp
SystemContext MakeCtx() {
    SystemContext ctx;
    ctx.system_root = "c:\\windows";
    ctx.system32_dir = "c:\\windows\\system32";
    ctx.syswow64_dir = "c:\\windows\\syswow64";
    ctx.program_data_dir = "c:\\programdata";
    ctx.temp_dir = "c:\\users\\tester\\appdata\\local\\temp";
    ctx.local_appdata_dir = "c:\\users\\tester\\appdata\\local";
    ctx.appdata_dir = "c:\\users\\tester\\appdata\\roaming";
    ctx.user_profile_dir = "c:\\users\\tester";
    ctx.public_dir = "c:\\users\\public";
    ctx.program_files_dir = "c:\\program files";
    ctx.program_files_x86_dir = "c:\\program files (x86)";
    ctx.own_pid = 4242;
    ctx.user_writable_roots = {
        "c:\\users\\tester\\appdata\\local\\temp",
        "c:\\users\\tester\\appdata\\local",
        "c:\\users\\tester\\appdata\\roaming",
        "c:\\users\\tester\\downloads",
        "c:\\users\\public"};
    ctx.removable_roots = {"e:\\"};
    return ctx;
}
AuditItem MakeProc(const char* name, const char* path) { /* kind=Process,
    pid=100, name/path set, signature=Unsigned default unless overridden */ }
```

Cases (each asserts specific rule IDs present/absent + score +
worst_severity):

```cpp
// 1. FAKE SVCHOST: name="svchost.exe", path="C:\\Users\\tester\\
//    AppData\\Local\\Temp\\svchost.exe", signature=Unsigned
//    → findings ⊇ {PT-SYSNAME-PATH(Critical), PT-SYSNAME-UNSIGNED(High),
//    PT-USERTEMP-UNSIGNED(Medium)}; score==100 (50+30+15 capped);
//    worst==Critical.
// 2. REAL SVCHOST: path="C:\\Windows\\System32\\svchost.exe",
//    signature=Trusted, signer="Microsoft Windows" → ZERO findings,
//    score 0, worst Info.
// 3. UNSIGNED NOTEPAD IN TEMP: name="notepad.exe", temp path,
//    Unsigned → ONLY PT-USERTEMP-UNSIGNED; score 15; worst Medium.
//    (PT-SYSNAME-* must NOT fire — notepad isn't a system name.)
// 4. SIGNED SYSTEMNAME WRONG SIGNER: svchost.exe in System32,
//    Trusted, signer="Evil Corp" → PT-SYSNAME-SIGNER Medium only;
//    score 15.
// 5. SERVICE: kind=Service, name="FakeSvc", path=temp exe,
//    start_type="auto", Unsigned → PT-SVC-USERTEMP(High) +
//    PT-SVC-UNSIGNED-AUTO(Medium); score 45; worst High.
//    Variant start_type="manual" → PT-SVC-UNSIGNED-AUTO is Low(5)
//    → score 35. Variant "disabled" → no SVC-UNSIGNED finding.
// 6. PROGRAM: kind=Program, name="Microsoft Fake Update",
//    publisher="", install_location="C:\\Users\\tester\\Downloads\\x"
//    → PT-PRG-MS-CLAIM(Low) + PT-PRG-USERTEMP(Low); score 10.
//    Control: publisher="Microsoft Corporation" → no MS-CLAIM.
// 7. EXEMPTIONS: pid==0 (Idle), pid==4 (System), pid==ctx.own_pid →
//    EvaluateAndFinalize adds ZERO findings even with a temp
//    svchost.exe path.
// 8. NO-PATH: proc with path="" → exactly PT-NO-PATH(Info), score 0.
// 9. NAME-ANOMALY: "svchost.exe .exe"/"evil .exe"/trailing-dot names
//    in a NEUTRAL dir (Program Files) unsigned → PT-NAME-ANOMALY
//    Medium fires; sysname rules don't (not in system set unless
//    name actually is — use "readme .txt.exe" style non-system name).
// 10. REPETITION: EvaluateAndFinalize twice → identical score/findings
//     count (idempotent; no duplicate findings).
// 11. AppendReputationFinding: Malicious→PT-REP-MALICIOUS Critical;
//     Suspicious→PT-REP-SUSPICIOUS High; Clean→nothing; Error→
//     PT-REP-ERROR Info. Then FinalizeSuspicion reflects them.
// 12. MsMpEng exemption: name="MsMpEng.exe",
//     path="C:\\ProgramData\\Microsoft\\Windows Defender\\Platform\\
//     4.18.1\\MsMpEng.exe", Trusted, signer="Microsoft Windows" →
//     ZERO findings (per-name allowed root works).
```

- [ ] **Step 2: Build — verify RED** (detection.h missing)

- [ ] **Step 3: Implement `detection.cc`**

Structure:

```cpp
namespace {
// Sorted constexpr table — binary_search or linear (18 entries).
constexpr std::string_view kSystemNames[] = {
    "conhost.exe","csrss.exe","dllhost.exe","dwm.exe","fontdrvhost.exe",
    "lsass.exe","lsaiso.exe","lsm.exe","msmpeng.exe","runtimebroker.exe",
    "securityhealthservice.exe","services.exe","sihost.exe","smss.exe",
    "sppsvc.exe","svchost.exe","taskhostw.exe","wininit.exe","winlogon.exe",
    "wmiprvse.exe"};
// Per-name extra allowed roots (normalized, lowercase):
// "msmpeng.exe" -> {"c:\\programdata\\microsoft\\windows defender"} —
// built in code from ctx.program_data_dir (normalize at eval time).

std::string BaseName(const std::string& path);   // after last '\\'/'/'
std::string Lower(std::string_view s);           // ToLowerAscii copy
bool IsSystemName(const std::string& base_lower);
bool PathUnderAny(const SystemContext& ctx,
                  const std::vector<std::string>& roots,
                  const std::string& normalized_path);
Severity WeightOf(Severity s);  // Info0 Low5 Medium15 High30 Crit50
}  // namespace
```

`EvaluateItemHeuristics` flow:
1. Early-out: `item->pid == 0 || item->pid == 4 ||
   item->pid == ctx.own_pid` → return. (`kind==Program` continues —
   only PRG rules apply; guard each rule by kind.)
2. `path_norm = NormalizePathForCompare(item->path)` once.
3. `base = Lower(BaseName(path_norm))` when path non-empty; also
   `name_lower = Lower(item->name)` — for services the image name
   comes from the resolved binary path; if `path` empty but `name`
   set, use name for sysname check? NO — sysname rules require a
   resolved PATH (a service NAMED svchost isn't an exe claim). Use
   path basename only; `PT-NO-PATH` covers the empty case.
4. Kind Process|Service shared rules:
   - `PT-NO-PATH`: path empty → Info finding, then `return`
     (no path rules possible).
   - `PT-SYSNAME-PATH`: IsSystemName(base) &&
     !(PathIsUnder(system32)||PathIsUnder(syswow64)||per-name-extra)
     → Critical. Per-name extra: `base=="msmpeng.exe"` &&
     PathIsUnder(normalize(ctx.program_data_dir+"\\microsoft\\
     windows defender"), path_norm).
   - `PT-SYSNAME-UNSIGNED`: IsSystemName(base) &&
     signature==Unsigned → High. (SignatureStatus::Error counts as
     NOT-unsigned for this rule? — spec: "signature == Unsigned"
     literally; Error → no finding, it's already noise. Document.)
   - `PT-SYSNAME-SIGNER`: IsSystemName(base) && Trusted &&
     signer lacks "microsoft" (case-insens substring) → Medium.
   - `PT-USERTEMP-UNSIGNED`: PathUnderAny(user_writable, path_norm)
     && signature==Unsigned → Medium.
   - `PT-NAME-ANOMALY` (Process only): inspect the FILENAME
     (basename of path, original case): ends_with '.' or ' ';
     contains ".exe." or ".scr." etc. double-ext pattern
     (`".exe."`/`".bat."`/`".cmd."`/`".scr."`/`".pif."` substring);
     name has chars Windows strips. Conservative: trailing
     dot/space OR `".exe."` substring → Medium.
5. Service-only:
   - `PT-SVC-USERTEMP`: PathUnderAny(user_writable, path_norm) →
     High (any signature state).
   - `PT-SVC-UNSIGNED-AUTO`: signature==Unsigned &&
     start_type∈{auto,boot,system}→Medium; =="manual"→Low;
     disabled/other→skip.
6. Program-only:
   - `PT-PRG-MS-CLAIM`: (name+" "+display_name+" "+publisher)
     contains "microsoft" or "windows" (lower) AND publisher lacks
     "microsoft" → Low.
   - `PT-PRG-USERTEMP`: install_location or uninstall_string looks
     like an absolute path (contains `":\\"` or starts `"\\\\"`)
     AND PathUnderAny(user_writable, normalize(it)) → Low.
     Expand env vars first? InstallLocation was env-expanded at
     enum time — normalize+compare directly.
7. Messages: `"<observed> — <why>"` style, e.g.
   `"svchost.exe resolves to C:\\Users\\…\\Temp\\svchost.exe outside
   system directories"`. Include the rule's concrete values — the
   details pane shows them verbatim.

`FinalizeSuspicion`: zero `item->findings` → score 0, worst Info;
else score=min(100,ΣWeightOf), worst=max severity (severity order
Info<Low<Medium<High<Critical). Recompute fresh — idempotent.

`AppendReputationFinding`: switch on `rep.verdict` —
Malicious→{Critical,"PT-REP-MALICIOUS","<provider> flags hash as
malicious (<m> malicious, <s> suspicious engines)"};
Suspicious→{High,"PT-REP-SUSPICIOUS","<provider>: <s> suspicious,
0 malicious"}; Error→{Info,"PT-REP-ERROR","reputation lookup
failed: <detail>"}; Clean/NotQueried→nothing. Only when
`rep.queried==true` for Malicious/Suspicious (Error implies queried
attempt anyway — append regardless of `queried` when verdict==Error
AND detail non-empty? simpler: append Error finding whenever
verdict==Error — the provider only produces Error after a real
attempt).

- [ ] **Step 4: Build + run — verify GREEN**

All 12 case groups pass. If a case fails, the bug is almost always
in `PathIsUnder`/`NormalizePathForCompare` interplay — recheck
trailing separators on roots like `"e:\\"`.

- [ ] **Step 5: Commit**

```bash
git add tools/process_task/detection.{h,cc} \
        tests/process_task/process_task_detection_tests.cc \
        tests/process_task/process_task_test_main.cc tests/CMakeLists.txt
git commit -m "Add ProcessTask heuristic detection engine"
```

---

## Task 5: Reputation providers + HTTP transport + settings store

The pluggable external-reputation layer and persistence.

**Files:**
- Create: `tools/process_task/http_transport.h`,
  `tools/process_task/http_transport.cc`
- Create: `tools/process_task/reputation.h`,
  `tools/process_task/reputation.cc`
- Create: `tools/process_task/settings_store.h`,
  `tools/process_task/settings_store.cc`
- Create: `tests/process_task/process_task_reputation_tests.cc`,
  `tests/process_task/process_task_settings_tests.cc`

**Interfaces:**
- Produces (per spec, namespace `XER::ProcessTask`):

```cpp
// http_transport.h
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual XER::Network::Http::SyncResponse Request(
        const std::string& method, const std::string& url,
        const std::string& body,
        const std::vector<std::string>& headers,
        long timeout_ms) = 0;
};
class SyncRequestTransport final : public IHttpTransport {
public:
    XER::Network::Http::SyncResponse Request(...) override;  // 1:1 wrap
};
std::shared_ptr<IHttpTransport> DefaultHttpTransport();

// reputation.h
class IReputationProvider { /* spec interface incl.
    ShouldStopBatch() */ };
std::unique_ptr<IReputationProvider> MakeMalwareBazaarProvider(
    std::shared_ptr<IHttpTransport> transport = DefaultHttpTransport());
std::unique_ptr<IReputationProvider> MakeVirusTotalProvider(
    std::string api_key,
    std::shared_ptr<IHttpTransport> transport = DefaultHttpTransport());
// Also: CollectLookupHashes — pure helper used by engine Phase C:
// dedupes item.sha256 over items where hashed==true && !sha256.empty()
// && kind != Program; SuspiciousOnly keeps only suspicion_score>0.
std::vector<std::string> CollectLookupHashes(
    const std::vector<AuditItem>& items, ReputationScope scope);

// settings_store.h
struct AppSettings {
    std::string provider = "malwarebazaar";   // malwarebazaar|virustotal|none
    std::string virustotal_api_key;
    std::string reputation_scope = "auto";     // auto|suspicious|all
    bool check_signatures = true;
    bool hash_files = true;
    bool use_reputation = true;
};
std::filesystem::path ProcessTaskSettingsPath();
bool LoadSettings(AppSettings* settings, std::string* error_out = nullptr);
bool SaveSettings(const AppSettings& settings,
                  std::string* error_out = nullptr);
// Path: %LOCALAPPDATA%/XER/ProcessTask/settings.json; non-Windows or
// missing env -> temp_directory_path()/"xer_process_task"/settings.json.
// LoadSettings: absent file -> defaults + true; corrupt -> defaults +
// false + error_out. SaveSettings: create_directories parent first.
```

- Consumes: `audit_types.h`, `content/network/http/
  http_sync_request.h`, `helper/jsoncpp_helper.h`, `helper/env_utils.h`,
  jsoncpp (`<json/json.h>` PRIVATE to reputation.cc/settings_store.cc).
- Produces: consumed by `AuditEngine` (Task 6) + GUI/CLI settings
  merge (Task 7).

- [ ] **Step 1: Write the failing tests**

`process_task_reputation_tests.cc` —
`XER_ProcessTask_RunReputationTests()`:

```cpp
class FakeTransport final : public IHttpTransport {
public:
    // queue of canned responses OR a std::function handler;
    // records every Request() into `calls` (method,url,body,headers).
    std::vector<...> calls;
    XER::Network::Http::SyncResponse Respond(
        const std::string& method, const std::string& url,
        const std::string& body,
        const std::vector<std::string>& headers, long timeout_ms) override;
};
// Helper: make SyncResponse{status,body,transport_ok=true}.

// MalwareBazaar:
//  - query_status=="ok" payload {"query_status":"ok","data":[{
//    "signature":"Emotet","file_name":"a.exe","first_submission":".."}]}
//    -> verdict Malicious, provider "malwarebazaar", detail contains
//    "Emotet", malicious/suspicious counts >=0 parsed defensively.
//  - {"query_status":"hash_not_found"} -> Clean, queried==true.
//  - {"query_status":"illegal_hash"} -> Error.
//  - transport_ok=false -> Error, detail carries transport error.
//  - HTTP 500 -> Error.
//  - Request shape: method POST, url == mb-api endpoint,
//    body contains "query=get_info" and "hash=<the sha>",
//    Content-Type form header present, timeout>0.
// VirusTotal (key="TESTKEY"):
//  - 200 {"data":{"attributes":{"last_analysis_stats":
//    {"malicious":5,"suspicious":2,"undetected":60,"harmless":30},
//    "meaningful_name":"EICAR"}}} -> Malicious, detail has EICAR.
//  - stats all-zero -> Clean.
//  - HTTP 404 -> Clean.
//  - HTTP 401 -> Error + ShouldStopBatch()==true.
//  - HTTP 429 -> Error + ShouldStopBatch()==true.
//  - empty key -> IsConfigured()==false; Lookup still callable but
//    returns Error("not configured") WITHOUT any transport call
//    (assert calls.empty()).
//  - GET shape: url contains /api/v3/files/<sha>, x-apikey header.
// CollectLookupHashes (pure):
//  - items: 2 procs same sha256, 1 svc different sha, 1 program with
//    sha set (must be EXCLUDED), 1 proc hashed==false (excluded),
//    1 proc empty sha (excluded).
//    AllCandidates -> {shaA, shaB} deduped; SuspiciousOnly with
//    scores {procA:0, procB:30} -> {shaB} only.
// DefaultScope: MB->AllCandidates, VT->SuspiciousOnly.
```

`process_task_settings_tests.cc` —
`XER_ProcessTask_RunSettingsTests()`:

```cpp
// Point LOCALAPPDATA at TempDir (setenv/_putenv_s on Windows —
//   XER::Helper::GetEnv reads process env; restore after).
// SaveSettings(all fields set) -> file exists -> LoadSettings ->
//   every field round-trips.
// Fresh temp dir (no file) -> LoadSettings true + all defaults.
// Write garbage bytes to settings.json -> LoadSettings false +
//   error_out non-empty + settings==defaults.
// Partial JSON {"provider":"virustotal"} -> loads, other fields
//   default.
```

- [ ] **Step 2: Build — verify RED**

- [ ] **Step 3: Implement `http_transport.{h,cc}`**

Trivial: `SyncRequestTransport::Request` →
`return XER::Network::Http::SyncRequest(method,url,body,headers,
timeout_ms,"XER-ProcessTask/1.0");` `DefaultHttpTransport()` →
`std::make_shared<SyncRequestTransport>()`. Header includes the
sync_request header + `<memory> <string> <vector>`.

- [ ] **Step 4: Implement `reputation.cc`**

- `MalwareBazaarProvider`: members `transport_`, `mutable
  bool stop_batch_{false}` (Lookup is const → mutable or const_cast;
  prefer mutable member — document). `Lookup`:
  - if stop_batch_ → return Error("batch stopped") without HTTP.
  - url `"https://mb-api.abuse.ch/api/v1/"`, body
    `"query=get_info&hash="+sha`, headers
    `{"Content-Type: application/x-www-form-urlencoded"}`, timeout
    20000.
  - `!r.transport_ok` → Error(r.error.empty()?"transport error":
    r.error). `r.status!=200` → Error("HTTP "+status). Also treat
    401/403/429 as stop_batch_=true (MB rate-limit behavior).
  - Parse `r.body` with jsoncpp (`Json::CharReaderBuilder` or
    `JsonCppHelper::Parse` — parse returns Value; check
    `.isObject()`). `query_status`:
    - `"ok"` → verdict Malicious; `data` array non-empty →
      detail = join present fields of `data[0]`: prefer
      `signature`, `file_name`, `first_submission`, `tags[0..2]`
      — build `"sig=<signature>; file=<file_name>; first=<ts>"`
      style, skip missing. `malicious`=1? NO — MB doesn't give
      engine counts; set `malicious=0,suspicious=0` and put the
      family in detail (spec: counts are VT-shaped; MB presence =
      malicious verdict, counts stay 0 — document).
    - `"hash_not_found"` → Clean (queried=true).
    - else → Error("query_status=<value>").
  - Parse failure → Error("malformed JSON response").
  - `Name()`→"malwarebazaar", `IsConfigured()`→true,
    `DefaultScope()`→AllCandidates.
- `VirusTotalProvider`: members `api_key_`, `transport_`,
  `mutable bool stop_batch_{false}`.
  - `IsConfigured()`→`!api_key_.empty()`; `Lookup` with empty key →
    Error("VirusTotal API key not configured") WITHOUT transport.
  - GET `"https://www.virustotal.com/api/v3/files/"+sha`, headers
    `{"x-apikey: "+api_key_}`, timeout 20000.
  - 404 → Clean; 401/403/429 → stop_batch_=true + Error
    ("unauthorized"/"rate limited"); other non-200 → Error.
  - 200 → parse `data.attributes.last_analysis_stats` →
    malicious/suspicious/undetected/harmless ints (default 0 when
    absent); verdict: malicious>0→Malicious; elif suspicious>0→
    Suspicious; else Clean. detail = `meaningful_name` or
    `type_description` when present ("EICAR").
  - `Name()`→"virustotal", `DefaultScope()`→SuspiciousOnly.
- `CollectLookupHashes`: iterate items; skip `kind==Program`,
  `!hashed`, `sha256.empty()`; dedupe via `std::unordered_set` +
  preserve first-seen order (vector + set); scope==SuspiciousOnly →
  additionally require `suspicion_score>0`; Auto treated as
  AllCandidates here (provider default is applied by the ENGINE when
  constructing — wait, no: scope resolution happens before this
  call; document that callers pass the RESOLVED scope, Auto never
  reaches here — assert/ignore Auto as AllCandidates defensively).
- `DefaultHttpTransport` in http_transport.cc.

- [ ] **Step 5: Implement `settings_store.cc`**

- `ProcessTaskSettingsPath()`: `GetEnv("LOCALAPPDATA")` non-empty →
  `<it>/XER/ProcessTask/settings.json` (UTF-8 string path — use
  `std::filesystem::path` u8string semantics carefully on Windows:
  build from `std::filesystem::path(local_appdata)` which takes
  UTF-16 natively? `GetEnv` returns UTF-8 std::string →
  `std::filesystem::u8path`? C++20: `std::filesystem::path(u8string)`.
  Use the `XER::Helper::Path` helpers if they expose
  string→path conversion — check `path_utils.h` API first and use
  its canonical helper (it exists precisely for this boundary).
  Fallback: `std::filesystem::temp_directory_path()/
  "xer_process_task"/settings.json`.
- `LoadSettings`: file absent (`ifstream` fails) → defaults, true.
  Parse via `JsonCppHelper::ParseFile` or jsoncpp directly; on parse
  failure → defaults + false + error_out "invalid JSON". Per-field
  reads with type checks (`isString`/`isBool`), missing → default.
  `provider` accepts only the three known strings else default.
- `SaveSettings`: `create_directories(parent)`, then
  `JsonCppHelper::WriteToFile(root, path)` — build `Json::Value`
  manually (version=1 + all fields) — returns false+error_out on
  failure. NOTE: WriteToFile takes a filesystem path string — check
  its signature (`filepath` std::string) and pass UTF-8.

- [ ] **Step 6: Build + run — verify GREEN**

Reputation parse matrix + settings round-trip pass. The live-MB
optional test: add ONE case guarded —
`SyncRequestTransport{}.Request("POST","https://mb-api.abuse.ch/
api/v1/","query=get_info&hash=<64 zeros>",...,5000)` → if
`!transport_ok` → skip silently (offline); else assert we got SOME
HTTP response (status>0) and Lookup on the zero-hash returns
Clean-or-Error gracefully (zero hash is `illegal_hash` → Error is
fine — assert verdict ∈ {Clean,Error}, NOT Malicious). This proves
end-to-end TLS+parse without depending on DB contents.

- [ ] **Step 7: Commit**

```bash
git add tools/process_task/http_transport.{h,cc} \
        tools/process_task/reputation.{h,cc} \
        tools/process_task/settings_store.{h,cc} \
        tests/process_task/process_task_reputation_tests.cc \
        tests/process_task/process_task_settings_tests.cc \
        tests/process_task/process_task_test_main.cc tests/CMakeLists.txt
git commit -m "Add ProcessTask reputation providers, HTTP transport, settings"
```

---

## Task 6: `audit_engine` orchestration + `report_export`

The worker-thread scan pipeline and the JSON/CSV/table exporters.

**Files:**
- Create: `tools/process_task/audit_engine.h`,
  `tools/process_task/audit_engine.cc`
- Create: `tools/process_task/report_export.h`,
  `tools/process_task/report_export.cc`
- Create: `tools/process_task/time_util.h` (header-only
  `UtcNowIso8601()` — shared by engine/export; refactor
  `process_enum.cc`'s local FILETIME→ISO helper to use it)
- Modify: `tools/process_task/audit_types.h` (add enum→string
  `inline const char*` name helpers used by export + GUI)
- Modify: `tools/process_task/process_enum.cc` (use `time_util.h`)
- Create: `tests/process_task/process_task_export_tests.cc`,
  `tests/process_task/process_task_engine_tests.cc`

**Interfaces:**
- Produces (per spec):

```cpp
// audit_engine.h
class AuditEngine {
public:
    explicit AuditEngine(
        std::shared_ptr<IHttpTransport> transport = nullptr);
    ~AuditEngine();
    AuditEngine(const AuditEngine&) = delete;
    AuditEngine& operator=(const AuditEngine&) = delete;
    bool Start(const ScanOptions& options,
               std::function<void(const ScanProgress&)> progress = {});
    void Cancel();
    bool IsRunning() const;
    ScanPhase Phase() const;
    ScanProgress Progress() const;
    bool Wait(int timeout_ms = 0);
    const ScanResult& Result() const;
private:
    void WorkerMain(ScanOptions options);
    void ReportProgress(ScanPhase, const char* stage,
                        int done, int total);
    // members: worker thread, mutex, cv?, atomics (cancel_, running_,
    // phase_), ScanProgress snapshot, ScanResult result_,
    // shared_ptr<IHttpTransport> transport_, progress callback
};
```

```cpp
// report_export.h
std::string FormatItemsTable(const ScanResult& result,
                             std::size_t max_rows = 50);
bool ExportJsonReport(const ScanResult& result,
                      const std::string& utf8_path,
                      std::string* error_out = nullptr);
bool ExportCsvReport(const ScanResult& result,
                     const std::string& utf8_path,
                     std::string* error_out = nullptr);
```

- Consumes: ALL prior modules; `helper/jsoncpp_helper.h` or direct
  jsoncpp, `helper/string_utils.h`.
- Produces: consumed by `main.cc` + `audit_widget` (Task 7) and tests.

- [ ] **Step 1: Write the failing tests**

`process_task_export_tests.cc` — `XER_ProcessTask_RunExportTests()`:

```cpp
// Build ScanResult fixture: hostname "testhost", started/finished
// stamps, options_used defaults; 3 items —
//   proc: name "svchost.exe", path temp, signature Unsigned,
//         sha256 "ab"*32, score 100, worst Critical,
//         findings {Critical/PT-SYSNAME-PATH/"..."},
//         reputation {queried, "malwarebazaar", Malicious, detail};
//   program: name "Foo Bar, Inc", publisher "Foo", score 0;
//   service: name "Svc", state "running", pid 88, score 15.
// ExportJsonReport -> TempDir/"report.json" ->
//   JsonCppHelper::ParseFile or jsoncpp parse -> assert:
//   format=="xer-process-task-report", version==1,
//   summary.items==3, summary.processes==1, flagged>=1,
//   items sorted score-desc (items[0].name=="svchost.exe"),
//   items[0].findings[0].rule=="PT-SYSNAME-PATH",
//   items[0].reputation.verdict=="malicious" (string form —
//   decide serialization: lowercase enum names).
// ExportCsvReport -> TempDir/"report.csv" -> read text ->
//   assert header line contains "kind,name,pid" (exact column list
//   pinned in impl), row count == 4 (header+3), the program row's
//   quoted name contains """Foo Bar, Inc""" or escaped form,
//   findings column contains "PT-SYSNAME-PATH".
// FormatItemsTable: non-empty result -> contains "svchost.exe" and
//   "PT-SYSNAME-PATH"; empty result -> returns header/"(no items)"
//   without crashing; max_rows respected.
// Export to a path in a NON-EXISTENT dir -> false + error_out
//   (create_directories NOT attempted — export writes exact path;
//   document: caller picks a valid dir, GetSaveFileName guarantees
//   it). Hmm — decide: ExportJsonReport DOES create_directories
//   (friendlier CLI). Pick: create parent dirs (one line, harmless).
//   Then test asserts success in nested temp subdir.
```

`process_task_engine_tests.cc` —
`XER_ProcessTask_RunEngineTests()`:

```cpp
#ifdef _WIN32
// FakeTransport returns {"query_status":"hash_not_found"} for every
// POST -> all reputation results Clean, zero rep findings.
// Engine e1(FakeTransport{}); ScanOptions opt; opt.use_reputation=
//   true; opt.provider_override="malwarebazaar";
//   opt.reputation_scope=AllCandidates; opt.check_signatures=false;
//   opt.hash_files=TRUE (need real hashes for lookups);
//   opt.use_reputation=true.
// Start with progress callback recording phases seen.
// Wait() -> Result().success==true, !cancelled, items non-empty,
//   processes>0; every proc/svc item has reputation.queried==true
//   OR verdict Clean (all Clean here); items sorted score desc
//   (verify non-increasing scores).
// Dedupe proof: count distinct sha256 among hashed proc/svc items
//   == number of POST calls FakeTransport recorded.
//   (FakeTransport counts calls — assert equality. THIS is the
//   money test for dedupe.)
// Cancel: Engine e2(real default transport); opt2.use_reputation=
//   false; opt2.check_signatures=TRUE; opt2.hash_files=TRUE (slow
//   enough); Start; poll Progress().phase until >=Analyzing (5s
//   cap) then Cancel; Wait; if Phase()!=Done assert
//   Result().cancelled==true && !success. (If it finished first —
//   fast machine — accept Done; the cancel path is also covered by
//   a second run: Start then IMMEDIATE Cancel before Wait — assert
//   final phase is Done|Cancelled|Failed, never hangs.)
// Wait-timeout: engine not started -> Wait(0) returns immediately?
//   spec: Wait on never-started returns true (nothing running) —
//   pin behavior: returns !IsRunning().
#else
// non-Windows: Start returns... spec: enumeration stubs yield empty
//   items but scan still "succeeds" with 0 items? DECIDE: on
//   non-Windows, WorkerMain still runs phases (enums return empty +
//   error strings — engine collects errors into result.error but
//   continues; success=true with 0 items is FINE for stubs).
//   Simpler: engine is platform-agnostic; empty enumeration is a
//   valid scan. Test asserts Start+Wait completes with success and
//   items.empty() — deterministic everywhere.
#endif
```

- [ ] **Step 2: Build — verify RED**

- [ ] **Step 3: Implement `audit_engine.cc`**

Members: `std::thread worker_; std::mutex mu_; std::condition_variable
cv_?` — Wait can poll `IsRunning()` with sleep loop (simpler, no cv
needed: `while(IsRunning()) Sleep(10)` — but Wait(timeout_ms) needs
deadline math; cv is cleaner: worker notifies on terminal phase.
Use `std::condition_variable` + `std::unique_lock` — repo uses
standard primitives; cv is fine).

`Start`: if `IsRunning()` → false. Copy options; reset result_
(success=false,cancelled=false,items.clear(),error.clear(),
options_used=options); phase_=Enumerating; spawn
`worker_=std::thread(&AuditEngine::WorkerMain,this,options)`;
invoke progress cb with initial snapshot (cb invoked from WORKER
thread — document: callbacks must be thread-safe; GUI marshals via
Tick polling instead of using cb — actually simpler: engine stores
progress snapshot; cb is OPTIONAL convenience. GUI uses Progress()
 polling, CLI uses Wait. Keep cb but document thread).

`WorkerMain` phases (check `cancelled_.load()` between EVERY item
and EVERY lookup — a tiny `bool Cancelled() const` helper):

```cpp
// A — Enumerate:
ReportProgress(Enumerating,"processes",0,3);
items += EnumerateProcesses(&err);  // errs collected non-fatally
items += EnumerateServices(&err);
items += EnumerateInstalledPrograms(&err);
// any enum error -> append to result.error notes ("; "-joined),
// do NOT fail the scan (partial data > none).
ReportProgress(Enumerating,"done",3,3);

// B — Analyze (proc+svc only for sig/hash; ALL kinds for heuristics):
SystemContext ctx = CollectSystemContext();
int total = items.size();
for i,item in items:
    if cancelled -> finalize(cancelled) return;
    if (item.kind != Program && !item.path.empty()) {
        if (opt.check_signatures)
            item.signature/signer <- CheckAuthenticode(path).status/.signer
            (store detail nowhere — signature detail isn't a model
            field; drop it, or append to findings? spec: detail is
            "UI+export" — AuditItem has no field; SKIP storing,
            document. Hmm — export loses the trust-hr detail. Add
            `signature_detail` field? audit_types is frozen from
            spec... spec AuditItem doesn't have it. Leave it out —
            status+signer suffice for v1.)
        if (opt.hash_files)
            item.hashed = Sha256FileHex(path,&item.sha256,&err);
    }
    EvaluateAndFinalize(&item, ctx);   // interim score for scope
    ReportProgress(Analyzing, stage, i+1, total);

// C — Reputation:
if (opt.use_reputation && !cancelled) {
    provider = MakeProvider(opt);   // "none"->nullptr; string map:
        // "malwarebazaar"->MakeMalwareBazaarProvider(transport_)
        // "virustotal"->MakeVirusTotalProvider(opt.reputation_api_key,transport_)
        // unknown/empty->malwarebazaar; "none"->skip phase
    if (provider && provider->IsConfigured()) {
        scope = opt.reputation_scope==Auto ? provider->DefaultScope()
                                           : opt.reputation_scope;
        hashes = CollectLookupHashes(items, scope);
        // hash -> vector<item*> map
        // 4 workers: atomic index into hashes; each: if cancelled or
        //   provider->ShouldStopBatch() -> skip rest (mark done);
        //   pacing: min 120ms between calls PER WORKER (steady_clock
        //   last-call); rep = provider->Lookup(hash); store in
        //   results[hash] (mutex).
        // join; apply: for each item with sha256 in results ->
        //   item.reputation = result; AppendReputationFinding;
        //   FinalizeSuspicion.
    }
}

// D — Finalize:
if (!cancelled) {
    sort items: suspicion_score desc, then name asc (stable);
    result.success=true; result.cancelled=false;
} else {
    result.success=false; result.cancelled=true; result.error="cancelled";
    // still sort partial items for consistent export
}
result.finished_utc = UtcNowIso(); result.hostname = ...;
phase_=Done/Cancelled/Failed; cv_.notify_all();
```

Helpers needed (local .cc): `UtcNowIso()` (same gmtime_s approach as
process_enum — DUPLICATE ~10 lines or share? Put `std::string
UtcNowIso8601()` in audit_types? It's not a type... put in
`system_context`? No — create tiny `time_util.h`? Simplest: static
function in audit_engine.cc AND process_enum.cc duplicates ~10
lines — acceptable but ugly. Better: add `std::string UtcNowIso8601()`
to `system_context.h`? It's not context... I'll spec a new tiny
header `tools/process_task/time_util.h` (header-only inline) used by
engine+enum+export — one place. Task 6 creates it; process_enum's
Task-2 local helper gets refactored to use it (small edit, same
commit). DECIDE: `time_util.h` header-only `inline std::string
UtcNowIso8601();` + `inline std::string FileTimeToIsoUtc(...)`? The
FILETIME one is Windows-only — keep FileTimeToIsoUtc local to
process_enum (it converts FILETIME→time_t→ISO via UtcNow-style
helper). `UtcNowIso8601` shared. And hostname: `gethostname`? Windows
`gethostname()` from winsock (ws2_32) or `GetComputerNameA/W` — use
GetComputerNameW→WideToUtf8 on _WIN32; non-Windows `gethostname`.
~10 lines in engine .cc.

`Wait(timeout_ms)`: `unique_lock lk(mu_); if(timeout==0) cv_.wait(lk,
[]{return !running_;}); else cv_.wait_for(lk, ms, pred)` — return
`!running_` i.e. `Phase()` terminal. `IsRunning` = phase not in
{Idle,Done,Failed,Cancelled}. `Phase()`/`Progress()` lock mu_.
`Result()` returns `result_` ref — document valid-after-Wait.
`Cancel()`: `cancelled_=true` (worker checks); does NOT join
(caller Waits or destructor joins).
`~AuditEngine`: `Cancel(); if(worker_.joinable()) worker_.join();`

`ReportProgress`: lock mu_, update progress_{phase,done,total,
stage}, phase_=phase (if terminal handled separately), invoke cb
OUTSIDE lock? cb from worker thread — invoke after unlocking to
avoid deadlock if cb calls Progress(). Copy snapshot, unlock, call.

- [ ] **Step 4: Implement `report_export.cc`**

- `FormatItemsTable`: fixed-width-ish text table to stdout string:
  header `"KIND      NAME                    PID    SCORE  WORST
  FINDINGS"` + rows `kind|name|pid|score|worst_sev|n_findings
  (first rule id)`; skip items with score==0 unless…? NO — print
  top max_rows by the ALREADY-sorted items order, include ALL kinds
  (CLI user sees everything; cap rows). Empty → `"(no items)"`.
  Format via `snprintf`/`absl::StrFormat`? — repo has fmt; use
  simple iostream/snprintf to avoid fmt dependency questions
  (XER_Network brings fmt anyway — `fmt::format` is fine, it's
  linked). Use fmt sparingly or plain snprintf — pick snprintf,
  zero dep risk.
- `ExportJsonReport`: build `Json::Value root` —
  `format`,`version:1`,`tool{name:"process_task",version:"dev"}`
  (version source = builder call; hardcode "dev" + comment),
  `scan{started_utc,finished_utc,cancelled,hostname,options{…all
  ScanOptions fields snake_case…}}`, `summary{processes,programs,
  services,items,flagged(score>0),by_severity{info..critical counts
  of worst_severity}}`, `items[]` — serialize EVERY AuditItem field
  (kind as "process"/"program"/"service" lowercase string;
  signature as lowercase status name; reputation.verdict lowercase;
  findings array of {severity,rule,message}; empty strings emitted
  as "" — explicit is better for consumers). Write via
  `JsonCppHelper::WriteToFile(root, path)` after
  `create_directories(parent_path)`.
- `ExportCsvReport`: header = pinned column list (spec it in code
  comment): `kind,name,display_name,pid,ppid,path,start_time_utc,
  service_name,state,start_type,binary_path_raw,service_dll,
  publisher,version,install_location,uninstall_string,install_date,
  signature,signer,sha256,hashed,rep_provider,rep_verdict,
  rep_malicious,rep_suspicious,rep_undetected,score,worst_severity,
  findings`. RFC-4180: quote field iff contains `,"`\n\r`; `"`→`""`.
  findings cell = `";"`-joined `rule|message`. Numbers plain.
  Write with `std::ofstream` binary + explicit `\n`? Use `\r\n`? —
  RFC says CRLF; Excel prefers it; use `\r\n` and document.
  `create_directories(parent)` same as JSON.
- Enum→string helpers: `ItemKindName`, `SeverityName`,
  `SignatureStatusName`, `ReputationVerdictName`, `ScanPhaseName` —
  lowercase strings, `static const char*` switches, `"unknown"`
  default. Put in report_export.cc anonymous namespace? GUI needs
  them too (details pane shows enum names) — put in `audit_types.h`
  as `inline const char*` functions? Header-only inline in
  audit_types.h is cleanest (no new TU, usable everywhere).
  ADD to audit_types.h in this task (it's our header — fine).

- [ ] **Step 5: Build + run — verify GREEN**

Export round-trips parse; engine completes real scan offline with
FakeTransport; dedupe count matches; cancel path exercised.

- [ ] **Step 6: Commit**

```bash
git add tools/process_task/audit_engine.{h,cc} \
        tools/process_task/report_export.{h,cc} \
        tools/process_task/time_util.h \
        tools/process_task/audit_types.h \
        tools/process_task/process_enum.cc \
        tests/process_task/process_task_export_tests.cc \
        tests/process_task/process_task_engine_tests.cc \
        tests/process_task/process_task_test_main.cc tests/CMakeLists.txt
git commit -m "Add ProcessTask scan engine and JSON/CSV export"
```

---

## Task 7: GUI widget + `main.cc` CLI + end-to-end verification

The visible tool: exe target, Win32 widget, CLI modes, final
verification pass.

**Files:**
- Create: `tools/process_task/audit_widget.h`,
  `tools/process_task/audit_widget.cc`
- Create: `tools/process_task/main.cc`
- Modify: `tools/process_task/CMakeLists.txt` (append exe target)

**Interfaces:**
- Produces:

```cpp
// audit_widget.h — WIN32 only content; header guarded so main.cc
// can include unconditionally under #if defined(_WIN32).
#ifdef _WIN32
namespace XER::ProcessTask {
class AuditPanelWidget final : public XER::UI::Widgets::Widget {
public:
    explicit AuditPanelWidget(XER::UI::Core::Window* owner);
    // Render/Layout/HandleEvent/Tick/GetMinSize/... overrides
    // per Widget contract; owns AuditEngine + children.
};
}
#endif
```

- Consumes: EVERYTHING — `XER_UI_Core` widgets (`Application`,
  `Window`, `AppTitleBar`, `Button`, `Label`, `Select`, `Checkbox`,
  `TextInput`, `Tabs`, `Table`, `Textarea`, `Badge`?), engine,
  settings_store, report_export, `helper/string_utils.h`.
- Produces: `process_task.exe` — GUI default, `--scan` CLI.

- [ ] **Step 1: Implement `audit_widget.{h,cc}`**

Composite widget per spec GUI layout (~700-900 lines, modeled on
`security_flash_panel_widget.cc` size/shape and `preview/main.cc`
chrome). Concrete structure:

- Members: `AuditEngine engine_{DefaultHttpTransport()}`;
  `AppSettings settings_` (loaded ctor); children:
  `Button scan_btn_, cancel_btn_, export_json_btn_, export_csv_btn_;`
  `Label status_label_, summary_label_;` `Select provider_select_,
  scope_select_;` `Checkbox sign_check_, hash_check_, rep_check_;`
  `TextInput api_key_input_;` `Tabs kind_tabs_;` three `Table`
  (`proc_table_, prog_table_, svc_table_`); `Textarea details_;`
  `Table findings_table_;` `std::shared_ptr<Window> owner_` if
  needed for dialogs (OPENFILENAMEW needs HWND — get via
  `window->NativeHandle()`? check Window API —
  `security_flash_panel_widget`/`projects_widget` obtain HWND
  somehow; grep `NativeHandle|hwnd` in src/ui/core/window.h first).
- `EnsureChildrenAttached()` pattern (panel-widget precedent):
  create + `AddChild`/attach widgets lazily on first Layout.
- Toolbar wiring:
  - Scan → build `ScanOptions` from widget state + settings merge:
    `provider_override`=provider_select text ("malwarebazaar"/
    "virustotal"/"none"); `reputation_scope`=scope_select
    ("auto"/"suspicious"/"all"→enum); `reputation_api_key`=
    api_key_input text; toggles from checkboxes; then
    `engine_.Start(opts)`; disable Scan, enable Cancel.
  - Cancel → `engine_.Cancel()`.
  - Provider/scope/checkbox/key changes → update `settings_` +
    `SaveSettings` (persist on change — cheap JSON write).
  - Export JSON/CSV → enabled only when `have_result_ &&
    result_.success`; `GetSaveFileNameW` (filter `JSON\\0*.json\\0`
    / `CSV\\0*.csv\\0`, default ext, title "Export audit report") →
    `ExportJsonReport/CsvReport(result_, WideToUtf8(path))` →
    status_label shows "exported to <path>" or error.
- `Tick(dt)`: if `engine_.IsRunning()` → `engine_.Progress()` →
  status_label "phase: stage done/total"; else if phase terminal &&
  !applied_ → pull `Result()`, `RebuildTables()`,
  `have_result_=result.success`, enable/disable export, summary
  label "N processes · M programs · K services · F flagged",
  applied_=true. Rescan resets applied_.
- `RebuildTables()`: for each kind filter `result_.items`, rows =
  string cells per spec columns; `Table::SetRows`; store row→item
  index map per table for click→details; findings count cell =
  `"<n> (<worst>)"`; score cell = number as string. Sort already
  done by engine (score desc) — STABLE within kind? Engine sorts
  globally; per-kind filter preserves relative order — good.
- `OnRowClick(table,index)`: resolve item → `details_` Textarea
  `SetText(FormatItemDetails(item))` (all fields, one per line,
  reputation block + signer + sha256) + `findings_table_` rows
  (Severity|Rule|Message). Read-only details.
- Keyboard: none required v1 (Tab between controls is native);
  Escape during scan → treat as Cancel? skip — keep simple.

- [ ] **Step 2: Implement `main.cc`**

- Top: `CommandLineArgsUtf8()` + `WideArgToUtf8` copied from
  `preview/main.cc:51-76` (same need — non-ASCII paths/keys).
- `PrintUsage()` — the CLI surface from spec.
- Arg parse (simple loop): `--scan`, `--json <path>`, `--csv
  <path>`, `--no-reputation`, `--reputation-scope all|suspicious`,
  `--provider malwarebazaar|virustotal|none`, `--api-key <key>`,
  `--no-signatures`, `--no-hash`, `--help|-h`. Unknown → usage+1.
- Settings merge helper `ScanOptions OptionsFromArgs(args,
  settings)`: start from `AppSettings` (LoadSettings), apply flags:
  provider_override = flag or settings.provider; scope = flag or
  settings.reputation_scope mapped ("suspicious"→SuspiciousOnly,
  "all"→AllCandidates, else Auto); api_key = `--api-key` > env
  `XER_PROCESS_TASK_API_KEY` (XER::Helper::GetEnv) >
  settings.virustotal_api_key; toggles = flags AND settings
  (effective = settings.X && !flag-disabled — since flags only
  DISABLE, `check_signatures = settings.check_signatures &&
  !no_signatures` etc; `use_reputation = settings.use_reputation &&
  !no_reputation && provider!="none"`).
- `RunScanCli(opts, json_path, csv_path)`:
  `AuditEngine engine(DefaultHttpTransport());` progress →
  `fprintf(stderr,"[%s] %d/%d %s\n",...)` throttled; `Start` →
  `Wait()` → on success print `FormatItemsTable(result)` to stdout +
  summary line `"scan complete: P processes, M programs, K services,
  F flagged (C critical, H high)"`; optional exports (create parent
  dirs handled inside export fns); exit 0 success / 1 fail /
  2 cancelled. Non-Windows: print "process enumeration is
  Windows-only" + exit 1 BEFORE engine start (or let engine produce
  empty success — DECIDE: explicit early-out is clearer UX; do
  `#ifndef _WIN32 → message+1`).
- `RunGui()` (`#ifdef _WIN32`): preview chrome pattern —
  `Application::Instance()`, `SetAppName("ProcessTask")`,
  `Initialize`, `Window::Create("XER ProcessTask", 1280, 800)`,
  `SetSizeLimits(960, 640, 0, 0)`, `SetCustomTitleBar(true, 38)`,
  `SetTheme(Dark)`, `AppTitleBar` window-command wiring, content =
  `AuditPanelWidget`, `SetFrameCallback([w]{w->TickTree(0.1);},100)`,
  `Show`, `Run`, `Shutdown`.
- `main`: `#if defined(_WIN32)` UTF8 argv path; dispatch
  `--scan`→RunScanCli, `--help`→usage, default→RunGui (WIN32) or
  "GUI requires Windows"+1 (non-WIN32).

- [ ] **Step 3: Append exe target to `tools/process_task/CMakeLists.txt`**

```cmake
add_executable(process_task main.cc)
target_include_directories(process_task PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_link_libraries(process_task PRIVATE
    XER_ProcessTask_Core
    XER_Flux_Core     # terminal styling for CLI output (preview parity)
)
target_compile_features(process_task PRIVATE cxx_std_23)
if(WIN32)
    target_sources(process_task PRIVATE audit_widget.cc)
    target_link_libraries(process_task PRIVATE XER_UI_Core)
endif()
if(MSVC)
    target_compile_options(process_task PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(process_task PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
set_target_properties(process_task PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
```

NOTE: `audit_widget.cc`/`audit_widget.h` are picked up by the core
glob — the `list(FILTER ... EXCLUDE REGEX "/(main|audit_widget)\\.cc$")`
from Task 1 already excludes the .cc; the .h is never globbed (only
*.cc are). Verify the exclude regex actually matches
(`/(main|audit_widget)\.cc$` — path uses forward slashes from
CMAKE_CURRENT_SOURCE_DIR on all generators; confirm at build time —
if the exe link shows duplicate `main()` or widget symbols in the
lib, fix the regex first).

- [ ] **Step 4: Build everything + run tests**

`"D:/Developers/XER3/out/vcenv.bat" cmake --preset default` (new exe
target) → build `process_task` + `XER_ProcessTask_Tests` →
`ctest -R ProcessTask --output-on-failure`. Expected: all suites
green including live enum/engine tests.

- [ ] **Step 5: CLI smoke**

```bash
out/build/default/bin/process_task.exe --help
out/build/default/bin/process_task.exe --scan --no-reputation
out/build/default/bin/process_task.exe --scan --no-reputation \
    --json "%TEMP%/pt_report.json" --csv "%TEMP%/pt_report.csv"
```

Expected: usage prints; scan completes offline (reputation skipped —
deliberately NOT testing live MB here; that's the optional test),
prints counts + top rows, JSON/CSV written — open JSON and eyeball
schema vs spec. If `--scan` hangs on signature checks, that's
WinVerifyTrust being slow on catalog files — expected ~seconds, not
minutes; if minutes, profile before shipping (possible infinite
loop in a parse path — check `QueryServiceConfigW` buffer loop and
the registry enum loop for missing increment/termination).

- [ ] **Step 6: GUI smoke (manual)**

Launch `process_task.exe` (no args) — window opens dark-themed,
Scan button populates tabs, suspicious rows sort to top, row click
fills details+findings, Export writes files, Cancel during a
signature-heavy scan stops promptly. (Builder does this by hand —
record result in commit message or progress notes.)

- [ ] **Step 7: Commit**

```bash
git add tools/process_task/audit_widget.{h,cc} tools/process_task/main.cc \
        tools/process_task/CMakeLists.txt
git commit -m "Add ProcessTask GUI and CLI"
```

---

## Self-review checklist (run before declaring done)

- [ ] Spec coverage: enumeration×3 ✓, heuristics+weights ✓,
      reputation pluggable (MB+VT) ✓, GUI table+export ✓,
      defensive-only ✓, no src/ changes ✓.
- [ ] Every rule ID in the spec table has a test case (Task 4).
- [ ] `main.cc`/`audit_widget.cc` excluded from core glob — VERIFY
      the built `XER_ProcessTask_Core.lib` contains no `main` symbol
      (`dumpbin /symbols` or link-error observation).
- [ ] No live network in default test path (FakeTransport everywhere;
      the one live probe self-skips).
- [ ] `/W4` clean on MSVC; `-Wall -Wextra -Wpedantic` clean elsewhere.
- [ ] `docs/wiki/decisions/process-task.md` open questions all
      resolved in spec (service enum=SCM, programs=uninstall keys,
      reputation=hash SHA-256, no SQLite).
- [ ] Progress doc: writer should add
      `docs/wiki/progress/process-task.md` summarizing what landed
      (house convention — phase-1a/1b have one).
