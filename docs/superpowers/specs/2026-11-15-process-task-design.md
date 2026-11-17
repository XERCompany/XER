# ProcessTask — Standalone Process/Program/Service Auditor Design

Date: 2026-11-15
Status: Scope approved (user rulings 2026-11-15); design spec
Decision record: `docs/wiki/decisions/process-task.md`

## Intent

A standalone Windows auditor — `tools/process_task/` — that enumerates
running processes, installed programs, and services, then scores each
item for "masquerading" suspicion: binaries claiming Windows system
names (`svchost.exe`, `lsass.exe`, …) from outside System32,
system-named executables in user/temp directories, unsigned "system"
binaries, service binary-path anomalies, and files flagged by an
external malware-reputation provider.

The "why": XER has no defensive-security tooling today. The repo
already contains the hard parts — a synchronous HTTPS client
(`XER::Network::Http::SyncRequest`), a retained-mode Win32+D2D+DWrite
widget kit (`XER_UI_Core`), OpenSSL for hashing, jsoncpp for export —
but nothing that enumerates the OS process/service/program surface or
verifies Authenticode signatures. ProcessTask is the consumer that
justifies adding those (all inside `tools/`, read-only on `src/`).

Defensive only: detection and reporting. No remediation, no killing,
no quarantine in v1.

## Goals

- Enumerate, on demand (manual rescan, not a daemon):
  - all running processes with PID, PPID, full image path, start time
  - all installed programs from the registry Uninstall keys
    (DisplayName, version, publisher, install location, uninstall
    string)
  - all services (name, display name, state, start type, PID,
    binary path, service DLL for svchost-hosted services)
- Heuristic detection engine: pure functions over plain records —
  every rule has a stable ID, severity, and human-readable message;
  produces a 0–100 suspicion score + worst-severity per item.
- Pluggable reputation provider: `IReputationProvider` interface;
  MalwareBazaar/abuse.ch (free, no key) as default;
  VirusTotal v3 optional behind a user-supplied API key.
  Reputation is hash-based (SHA-256 of the resolved binary).
- GUI: `XER_UI_Core` window — toolbar, kind tabs
  (Processes | Programs | Services), sortable-feeling table per kind,
  details pane with findings, live progress + cancel during scans.
- Export: JSON and CSV reports of the last completed scan.
- Headless CLI: `process_task --scan` runs the same engine without UI
  (also the automated test path).
- Persist only settings (provider choice, API key, toggles) — scans
  are ephemeral; export covers history. **No SQLite catalog**
  (resolved ambiguity — the decision doc's own lean; a process audit
  is a point-in-time snapshot, not a media library).

## Non-goals (v1)

- Remediation of any kind: no kill/suspend/delete/quarantine,
  no "fix" buttons, no Windows Defender integration.
- Process command lines and per-process user identity — reserved
  fields in the data model, always empty in v1 (see "Resolved
  ambiguities"). WMI/NtQueryInformationProcess adds real complexity
  for marginal heuristic gain in v1.
- Real-time monitoring / polling / alerting — scan-on-demand only.
- VirusTotal "re-scan file" uploads, sandbox detonation, YARA rules,
  sigma rules, memory forensics, network connections, drivers list,
  autoruns beyond services, scheduled tasks.
- Installed-program signature checks and hashing by default (see
  "Scan depth" below — too slow for hundreds of entries; programs get
  path/name heuristics only).
- Row coloring in the table widget (the `Table` widget has no
  per-row color API — suspicion is shown via score text + details
  pane; noted under Risks).
- Cross-platform: the tool is Windows-only by definition. Core code
  compiles everywhere with `_WIN32` guards + "not supported" stubs
  (security_flash pattern) so tests/CI stay portable; the GUI and all
  enumeration are Windows-only.
- Authenticode revocation checking (needs network + slows scans;
  `WTD_REVOKE_NONE`).

## Anti-goals

- Scanning inside `src/` — all new code lives in `tools/process_task/`
  and `tests/process_task/`; `src/` is read-only for this feature.
- Loading whole file hashes into RAM — SHA-256 streams in 1 MiB chunks.
- Blocking the UI thread during scans — enumeration/analysis/reputation
  run on a worker thread; the widget polls progress in `Tick`.
- A resident service or tray icon — it's an auditor you run.
- Treating "not found in MalwareBazaar" as proof of cleanliness — it
  only means "not in that DB"; findings say exactly that.

## Constraints (verified facts)

- **`Process::ListProcesses()` is insufficient alone**
  (`src/content/process/process.cc:532-592`): Toolhelp32 snapshot
  fills `ProcessInfo.executable_path` with `pe32.szExeFile` — the
  image **name** only (e.g. `svchost.exe`), not a full path. No PPID,
  no user, no session, no start time (`process.h:38-47`). PPID IS
  available in the same snapshot (`th32ParentProcessID`) — our own
  enumerator captures it. `XER_ContentProcess` already links `psapi`
  (`src/content/process/CMakeLists.txt`) but we don't need it: the
  tool does its own Win32 enumeration in `tools/process_task/`.
- **No service enumeration exists.** Only single-service SCM
  query/control precedents:
  `src/ui/widgets/security_flash_panel_widget.cc:50-152`
  (`OpenSCManagerW`/`OpenServiceW`/`QueryServiceStatusEx`/
  `StartServiceW`/`ControlService`, RAII `ScmHandle` at :38-46) and
  install/uninstall in `tools/SecurityFlashService/main.cc:473-545`.
  `EnumServicesStatusExW` + per-service `QueryServiceConfigW` is new
  code. `SERVICE_STATUS_PROCESS.dwProcessId` gives the service→PID
  link for running services.
- **No installed-programs enumeration exists.** Only raw registry
  calls for the Explorer context menu (`tools/preview/main.cc:288-301`,
  `RegCreateKeyExW`/`RegSetValueExW`/`RegDeleteTreeW` on HKCU). The
  Uninstall-key walk (HKLM 64-bit + `WOW6432Node` + HKCU + HKCU
  `WOW6432Node`) is new code; `advapi32` registry APIs directly.
- **No Authenticode verification exists anywhere** — grep for
  `WinVerifyTrust`/`CryptQueryObject`/`ImageGetCertificateData` across
  `src/`, `tools/`, `tests/` returns nothing. `crypt32` is already
  linked by several targets for other purposes; **`wintrust.lib` is
  not linked anywhere** — the tool's CMake must add it. New module
  `pe_signature.cc` wraps `WinVerifyTrust` +
  `WTHelperGetProvSignerFromChain` for the signer CN.
- **HTTP client**: `XER::Network::Http::SyncRequest`
  (`src/content/network/http/http_sync_request.h:29-34`, impl
  `http_sync_request.cc:53-109`) — synchronous one-shot libcurl,
  `SyncResponse{status, body, headers(lowercase), error,
  transport_ok}`, `timeout_ms` bounds the exchange, connect capped at
  15 s. Target `XER_Network` (`src/content/network/CMakeLists.txt:17`)
  PUBLIC-links `OpenSSL::SSL`/`OpenSSL::Crypto`, `CURL::libcurl`,
  `absl`, `fmt`, `jsoncpp_lib`, `XER_Hex`, `XER_AsyncIO`, `XER_Cache`,
  `XER_Flux_Core`, `ZLIB`. Existing consumers: `s3_client.cc:81`,
  `node_graph_app_nodes.cc:517`. Reputation lookups go through a
  thin `IHttpTransport` seam (default impl wraps `SyncRequest`) so
  tests inject a fake — no live network in unit tests.
- **SHA-256**: OpenSSL one-shot `SHA256()` used in
  `src/content/s3/s3_signer.cc:44-46`; EVP in
  `src/security_flash/token_store.cc:71`. We stream via EVP
  (`EVP_DigestInit_ex`/`Update`/`Final`) — executables can be large.
  `XER::Content::Hex::Encode` (`src/content/hex/hex.h:38-55`) is
  header-only byte→hex; `XER_Hex` is an INTERFACE target.
- **JSON**: `jsoncpp_lib` + `XER::Helper::JsonCppHelper`
  (`src/helper/jsoncpp_helper.h`) — same stack as
  `tools/video_editor/project_store`.
- **GUI kit**: `XER_UI_Core` (`src/ui/CMakeLists.txt:45`) — Skia +
  D2D + DWrite, links `comctl32`, `user32/gdi32/d2d1/d3d11/d3d12/dxgi/
  dwrite/comdlg32/windowscodecs/ole32/dwmapi/gdiplus`, defines
  `WIN32_LEAN_AND_MEAN NOMINMAX`. Relevant widgets:
  `Widgets::Table` (`src/ui/widgets/table.h:15-64` — header + string
  cells, `SetColumns/SetRows/SetSelectedRow/SetOnRowClick/
  SetOnCellClick/ScrollTo`; `Column.sortable` flag exists but **no
  sort implementation in `table.cc`** — sorting is a builder call,
  see Risks), `Tabs` (`tabs.h` — Pill/Underline, owner swaps panels),
  `Textarea` (`SetText`/`SetReadOnly`), `Select` (`SetOptions`/
  `GetSelectedIndex`), `Button`, `Label`, `Badge`, `AppTitleBar`.
  Window/app bootstrap precedent: `tools/preview/main.cc:353-422`
  (frameless chrome, `SetFrameCallback` → `TickTree`, dark theme).
- **Tools pattern**: `tools/video_editor/CMakeLists.txt` — own
  CMakeLists creating `XER_VideoEditor_Core` STATIC via
  `file(GLOB CONFIGURE_DEPENDS *.cc)` (:9-13), PUBLIC include dirs
  `src/` + self, PUBLIC links, `cxx_std_23`, `/W4 /WX- /O2` on MSVC.
  Root wires it at `CMakeLists.txt:476` (`add_subdirectory`). The
  future `main.cc` must be **excluded from the glob** (Phase-1C plan
  Task 7) — we exclude `main.cc` AND `audit_widget.cc` from the start.
  `tools/preview` is the exe precedent: inline `add_executable` in root
  `CMakeLists.txt:442-464`, GUI `.cc` files gated `if(WIN32)
  target_sources`, `XER_UI_Core` linked only on WIN32, `main.cc`
  re-parses argv as UTF-16→UTF-8 (`preview/main.cc:65-76` — needed
  for non-ASCII paths), non-Windows falls back to CLI text.
- **Namespace precedent**: `XER::VideoEditor`, `XER::SecurityFlash`
  for library-grade tools; `preview::` for preview's internals. We use
  **`XER::ProcessTask`** throughout `tools/process_task/` and
  `tests/process_task/`.
- **Test precedent**: `XER_VideoEditor_Tests`
  (`tests/CMakeLists.txt:84-98`) — custom `*_test_main.cc` calling
  suite runners, shared failure counter + `XER_VE_CHECK`-style macro
  in `*_test_util.h`, plain asserts, `%TEMP%` fixtures via
  `TempDir()`, self-skip via capability checks. We mirror it as
  `XER_ProcessTask_Tests` under `tests/process_task/`.
- **String/path helpers**: `XER::Helper::String`
  (`src/helper/string_utils.h` — `StartsWith/EndsWith/Split/
  ReplaceAll/ToLowerAscii/TrimAsciiWhitespaceCopy/Utf8ToWide/
  WideToUtf8`), `XER::Helper::Path` (`path_utils.h` — UTF-8↔UTF-16
  boundary helpers, REQUIRED for Win32 path calls),
  `XER::Helper::GetEnv` (`env_utils.h`).
- **File dialogs**: `OPENFILENAMEW` + `GetOpenFileNameW` precedent at
  `projects_widget.cc:53-67`, `video_preview_widget.cc:32-43`;
  `GetSaveFileNameW` at `node_graph_editor_widget.cc:196`.
- **Non-Windows convention**: security_flash reports "not supported"
  off-Windows (`windows_account.h:11`); GUI sources guard with
  `#if defined(_WIN32)` (`preview/main.cc:33,424`). Same here.
- Branch `feature/xer-studio-1c-export`; `docs/` and `.superpowers/`
  are untracked — writing specs/plans there is expected.

## Architecture

```
tools/process_task/
  CMakeLists.txt          XER_ProcessTask_Core STATIC + process_task exe
  main.cc                 arg parse → --scan CLI | GUI bootstrap (WIN32)
  audit_types.h           enums + POD records (header-only)
  system_context.{h,cc}   OS dirs/env → normalized path sets for rules
  process_enum.{h,cc}     Toolhelp32 + QueryFullProcessImageNameW → items
  service_enum.{h,cc}     EnumServicesStatusExW + QueryServiceConfigW
  program_enum.{h,cc}     Uninstall registry keys (4 views) → items
  pe_signature.{h,cc}     WinVerifyTrust + signer CN extraction
  file_hash.{h,cc}        streaming SHA-256 → lowercase hex
  http_transport.{h,cc}   IHttpTransport + SyncRequestTransport adapter
  reputation.{h,cc}       IReputationProvider + MalwareBazaar + VirusTotal
  settings_store.{h,cc}   %LOCALAPPDATA%/XER/ProcessTask/settings.json
  detection.{h,cc}        heuristic rules → findings + score (pure)
  audit_engine.{h,cc}     scan orchestration: phases, progress, cancel
  report_export.{h,cc}    JSON + CSV writers + CLI table formatter
  audit_widget.{h,cc}     GUI panel — WIN32 only, exe-side (not in core lib)
```

All modules are UI-free except `audit_widget` + the GUI branch of
`main.cc`. `XER_ProcessTask_Core` is consumed by both the exe and
`XER_ProcessTask_Tests` — everything testable headless lives in core.

Threading: `AuditEngine` owns one worker `std::thread`. Phase A
(enumerate) and Phase B (per-item signature+hash+path heuristics) are
sequential but yield progress per item; Phase C (reputation) runs a
small fixed pool (4 workers) over deduplicated hashes with
per-provider pacing; `Cancel()` sets an atomic checked between items
and between HTTP calls. GUI `Tick` polls `engine->Progress()` /
`engine->Result()`; CLI calls `Wait()`.

## Data model

`audit_types.h` (namespace `XER::ProcessTask`, header-only):

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
    std::string message;   // human-readable, includes observed values
};

struct ReputationResult {
    bool queried = false;
    std::string provider;                    // "malwarebazaar" | "virustotal"
    ReputationVerdict verdict = ReputationVerdict::NotQueried;
    int malicious = 0, suspicious = 0, undetected = 0, harmless = 0;
    std::string detail;                      // family name / error note
};

struct AuditItem {
    ItemKind kind = ItemKind::Process;
    // identity (all kinds)
    std::string name;          // image name / service name / program name
    std::string display_name;  // service DisplayName; == name otherwise
    int pid = 0;               // process pid; running service's pid
    int ppid = 0;              // process only
    std::string path;          // resolved UTF-8 path (exe / svc binary /
                             //   install location); "" when unavailable
    std::string command_line;  // RESERVED v1 — always empty
    std::string user;          // RESERVED v1 — always empty
    std::string start_time_utc;// process creation time; "" otherwise
    // service-only fields (empty for other kinds)
    std::string service_name;
    std::string binary_path_raw;   // lpBinaryPathName verbatim
    std::string start_type;        // auto|manual|disabled|boot|system
    std::string state;             // running|stopped|start_pending|...
    std::string service_dll;       // Parameters\ServiceDll for svchost
    // program-only fields
    std::string publisher;
    std::string version;
    std::string install_location;
    std::string uninstall_string;
    std::string install_date;
    // analysis results
    SignatureStatus signature = SignatureStatus::NotChecked;
    std::string signer;            // cert subject CN when signed
    bool hashed = false;
    std::string sha256;            // lowercase hex
    ReputationResult reputation;
    std::vector<Finding> findings;
    int suspicion_score = 0;             // 0..100
    Severity worst_severity = Severity::Info;
};

struct ScanOptions {
    bool check_signatures = true;    // WinVerifyTrust per candidate file
    bool hash_files = true;          // SHA-256 per candidate file
    bool use_reputation = true;      // provider lookups
    ReputationScope reputation_scope = ReputationScope::Auto;
    std::string provider_override;   // "" = settings/default
    std::string reputation_api_key;  // resolved VT key (CLI>env>settings)
};

struct ScanProgress {
    ScanPhase phase = ScanPhase::Idle;
    int items_done = 0;
    int items_total = 0;
    std::string stage;               // e.g. "hashing", "reputation"
};

struct ScanResult {
    bool success = false;
    bool cancelled = false;
    std::string error;
    std::vector<AuditItem> items;    // sorted by suspicion_score desc
    std::string started_utc;
    std::string finished_utc;
    ScanOptions options_used;
    std::string hostname;
};
```

`AuditItem` is deliberately one flat struct: the GUI table, JSON, and
CSV all consume it uniformly; kind-specific fields are simply empty
for other kinds. (Resolved ambiguity — a `std::variant` of three
records was considered; flat struct wins on export simplicity.)

## Enumeration sources

### Processes — `process_enum.cc` (Win32)

`CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` gives PID, PPID
(`th32ParentProcessID`), image name. Per PID:
`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` →
`QueryFullProcessImageNameW` (kernel32 — full path, UTF-16 → UTF-8
via `XER::Helper::String::WideToUtf8`), `GetProcessTimes` → creation
time → ISO-8601 UTC, `ProcessIdToSessionId`. Access-denied/protected
processes (MsMpEng, csrss for non-elevated) keep `path=""` + an Info
finding. Pseudo-PIDs 0 (Idle) and 4 (System) are recorded with no
findings. Own PID is recorded but exempt from suspicion findings
(the auditor flags itself otherwise when run from odd dirs).

### Services — `service_enum.cc` (Win32)

`OpenSCManagerW(SC_MANAGER_ENUMERATE_SERVICE | CONNECT)` →
`EnumServicesStatusExW(SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
SERVICE_STATE_ALL)` in 64 KiB buffer loop → per service
`OpenServiceW(SERVICE_QUERY_CONFIG)` → `QueryServiceConfigW` for
`lpBinaryPathName`, `dwStartType`, `lpServiceStartName`. Binary path
normalization: expand `%SystemRoot%`/env vars
(`ExpandEnvironmentStringsW`), strip surrounding quotes, take the
first token as the image path (svchost command lines carry `-k`
args), UTF-16→UTF-8. For services whose binary basename is
`svchost.exe`, read
`HKLM\SYSTEM\CurrentControlSet\Services\<name>\Parameters\ServiceDll`
into `service_dll` (the real payload — materially better context;
only for svchost-hosted, bounded registry reads). `state` from
`SERVICE_STATUS_PROCESS.dwCurrentState`, `pid` from
`dwProcessId` (0 when not running). Kernel drivers included with
whatever `lpBinaryPathName` reports (`\SystemRoot\...` / `\??\`
prefixes normalized where trivially possible — best-effort, noted).

### Installed programs — `program_enum.cc` (Win32)

Enumerate `Uninstall` subkeys under four registry views:
`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall`,
`HKLM\SOFTWARE\WOW6432Node\...\Uninstall`,
`HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall`,
`HKCU\SOFTWARE\WOW6432Node\...\Uninstall`. Per subkey read
`DisplayName` (skip entries lacking it — bundle parents),
`DisplayVersion`, `Publisher`, `InstallLocation`, `UninstallString`,
`InstallDate`, `EstimatedSize`, `SystemComponent` (skip =1).
Dedupe across views by lowercase `UninstallString`+`InstallLocation`
key. `kind=Program`; `path` = `InstallLocation` (may be "");
`name` = `DisplayName`. No signature/hash/reputation in v1 —
heuristics only (see Scan depth).

### Scan depth (resolved ambiguity)

Signature checks and SHA-256 hashing apply to **process images and
service binaries only** — the masquerade threat surface and a
bounded set (~hundreds, deduped by path). Installed programs get
metadata + path/name heuristics only; a "deep scan" toggle is a
builder-call later (hundreds of WinVerifyTrust calls is minutes of
work). Reputation lookups consume the hashes computed in Phase B —
programs are never looked up in v1.

## Detection engine — `detection.cc` (pure)

`EvaluateItem(const AuditItem&, const SystemContext&)` appends
`Finding`s; `FinalizeScore(AuditItem&)` sums weights →
`suspicion_score = min(100, Σ)` and `worst_severity = max`. All
rules are pure functions of the record + `SystemContext` — fully
unit-testable with synthetic items, no Win32 needed.

`SystemContext` (from `system_context.cc`, once per scan):
`system_root`, `system32`, `syswow64`, `program_data`,
`temp`, `local_appdata`, `appdata`, `user_profile`, `public`,
`program_files`, `program_files_x86`, `own_pid`
(`GetCurrentProcessId()` — the auditor exempts itself from findings),
plus derived `user_writable_roots` (temp, local_appdata, appdata,
downloads, public, removable drive roots via `GetDriveTypeW`).
Per-name allowed roots for the system-name rule live in
`detection.cc` as a small table (`MsMpEng.exe` →
`%ProgramData%\Microsoft\Windows Defender` — Defender's real install
path is outside System32). All comparisons are
case-insensitive, normalized, trailing-backslash-aware
(`XER::Helper::String` + a small path-normalize helper in
`system_context.cc`).

### System-name set (process/service image basenames, case-insensitive)

Core Windows processes that must live in System32/SysWOW64:
`svchost.exe lsass.exe csrss.exe smss.exe wininit.exe winlogon.exe
services.exe lsaiso.exe lsm.exe fontdrvhost.exe dwm.exe sihost.exe
taskhostw.exe runtimebroker.exe dllhost.exe conhost.exe wmiprvse.exe
sppsvc.exe securityhealthservice.exe msmpeng.exe`. Deliberately
EXCLUDES names legitimately launched from user dirs by design
(`explorer.exe` lives in `Windows\`, browsers, etc.). The set is a
`constexpr` table in `detection.cc` — easy to extend.

### Heuristic rules (v1)

| Rule ID | Applies to | Condition | Severity (weight) |
|---|---|---|---|
| `PT-SYSNAME-PATH` | proc, svc | image basename ∈ system names AND resolved path NOT under allowed roots (System32/SysWOW64 + per-name extras) | Critical (50) |
| `PT-SYSNAME-UNSIGNED` | proc, svc | basename ∈ system names AND signature == Unsigned (incl. NotChecked→skipped; only evaluated when signature known) | High (30) |
| `PT-SYSNAME-SIGNER` | proc, svc | basename ∈ system names AND signature == Trusted AND signer CN lacks "Microsoft" | Medium (15) |
| `PT-USERTEMP-UNSIGNED` | proc, svc | path under user-writable roots AND signature == Unsigned | Medium (15) |
| `PT-SVC-USERTEMP` | svc | service binary path under user-writable roots (regardless of signature) | High (30) |
| `PT-SVC-UNSIGNED-AUTO` | svc | signature == Unsigned AND start_type == auto → Medium (15); manual → Low (5); disabled → no finding |
| `PT-NAME-ANOMALY` | proc | filename has trailing `.`/space, double extension (`*.exe.*`), or name≠stem mismatch Windows would strip | Medium (15) |
| `PT-REP-MALICIOUS` | proc, svc | reputation verdict Malicious | Critical (50) |
| `PT-REP-SUSPICIOUS` | proc, svc | reputation verdict Suspicious (VT suspicious>0 or MB-family partial) | High (30) |
| `PT-REP-ERROR` | proc, svc | reputation lookup failed (network/HTTP) | Info (0) — transparency only |
| `PT-NO-PATH` | proc, svc | path empty (access denied / exited / kernel) | Info (0) |
| `PT-PRG-MS-CLAIM` | program | DisplayName/Publisher claims Microsoft/Windows but Publisher field missing or lacks "Microsoft" | Low (5) |
| `PT-PRG-USERTEMP` | program | InstallLocation or UninstallString under user-writable roots | Low (5) |

Notes:
- `PT-SYSNAME-PATH` fires even when unsigned (unsignedness adds its
  own findings) — a fake `svchost.exe` in `%TEMP%` collects
  SYSNAME-PATH (50) + SYSNAME-UNSIGNED (30) + USERTEMP-UNSIGNED (15)
  = capped 100. A *signed* fake still hits 50 — correct, that's the
  core masquerade case.
- Reputation "Clean" (queried, zero detections) produces NO finding —
  absence of evidence isn't evidence; the `reputation` block in
  export still records the negative lookup for audit trail.
- Programs never receive signature/hash/reputation findings in v1.
- Findings are ordered by rule evaluation order; `worst_severity` =
  max severity present; ties in `suspicion_score` sort by name.

### Reputation provider — `reputation.{h,cc}`

```cpp
struct ReputationResult { /* as above */ };

class IReputationProvider {
public:
    virtual ~IReputationProvider() = default;
    virtual std::string Name() const = 0;                    // "malwarebazaar"|"virustotal"
    virtual bool IsConfigured() const = 0;                   // VT: key present
    virtual ReputationScope DefaultScope() const = 0;        // MB: AllCandidates; VT: SuspiciousOnly
    // One SHA-256 hex lookup. Never throws; transport/HTTP/parse
    // failures → verdict=Error + detail. "Not in DB" → Clean.
    virtual ReputationResult Lookup(const std::string& sha256_hex,
                                    std::string* error_out = nullptr) const = 0;
    // True once the provider knows further lookups will fail the same
    // way (e.g. VT 401 bad key / 429 quota) — the engine drains the
    // remaining queue as NotQueried instead of hammering the API.
    virtual bool ShouldStopBatch() const { return false; }
};

class IHttpTransport {   // http_transport.h
public:
    virtual ~IHttpTransport() = default;
    virtual XER::Network::Http::SyncResponse Request(
        const std::string& method, const std::string& url,
        const std::string& body,
        const std::vector<std::string>& headers,
        long timeout_ms) = 0;
};
// Default impl wraps XER::Network::Http::SyncRequest (one-line adapter).

std::unique_ptr<IReputationProvider> MakeMalwareBazaarProvider(
    std::shared_ptr<IHttpTransport> transport = DefaultHttpTransport());
std::unique_ptr<IReputationProvider> MakeVirusTotalProvider(
    std::string api_key,
    std::shared_ptr<IHttpTransport> transport = DefaultHttpTransport());
```

**MalwareBazaar** (default): `POST https://mb-api.abuse.ch/api/v1/`,
form body `query=get_info&hash=<sha256>`,
`Content-Type: application/x-www-form-urlencoded`, ~20 s timeout.
Parse `query_status`: `"ok"` → verdict Malicious, detail =
`data[0].signature`/`file_name`/`first_submission` (field names
verified defensively at impl — parse what's present); `"hash_not_found"`
→ Clean; anything else/HTTP≠200 → Error. No API key. Free, anonymous
tier is adequate for deduplicated lookups (~unique binaries, typically
<200/scan).

**VirusTotal v3** (optional): `GET
https://www.virustotal.com/api/v3/files/<sha256>`, header
`x-apikey: <key>`. 200 → `data.attributes.last_analysis_stats` →
malicious>0 ⇒ Malicious; else suspicious>0 ⇒ Suspicious; else Clean
(detail carries `meaningful_name`/`type_description` when present).
404 ⇒ Clean (not in VT). 401 ⇒ Error "invalid API key"; 429 ⇒ Error
"rate limited" — engine stops the VT queue on first 429 and marks
remaining `NotQueried` (free tier ≈ 4 lookups/min — hence VT's
`DefaultScope() = SuspiciousOnly`). Key sources, in priority order:
CLI `--api-key` > env `XER_PROCESS_TASK_API_KEY` > settings.json.

**Scope resolution**: `ScanOptions.reputation_scope == Auto` ⇒
provider's `DefaultScope()`. `SuspiciousOnly` ⇒ lookups only for
items with `suspicion_score > 0` after Phase B; `AllCandidates` ⇒
every unique SHA-256 among process/service items. Hashes dedupe by
digest — 30 `svchost.exe` instances cost ONE lookup. Lookups run on
4 worker threads, ~120 ms pacing between requests per worker;
cancellable between calls.

## Audit engine — `audit_engine.{h,cc}`

```cpp
class AuditEngine {
public:
    // transport defaults to the SyncRequest-backed adapter; tests
    // inject a fake so reputation paths run fully offline.
    explicit AuditEngine(
        std::shared_ptr<IHttpTransport> transport = nullptr);
    ~AuditEngine();                    // joins worker; cancels if running
    AuditEngine(const AuditEngine&) = delete;
    AuditEngine& operator=(const AuditEngine&) = delete;

    bool Start(const ScanOptions& options,
               std::function<void(const ScanProgress&)> progress = {});
    void Cancel();                     // idempotent, thread-safe
    bool IsRunning() const;
    ScanPhase Phase() const;           // last reported phase (mutex'd)
    ScanProgress Progress() const;     // thread-safe snapshot
    bool Wait(int timeout_ms = 0);     // 0 = infinite; false on timeout
    const ScanResult& Result() const;  // valid after Wait(); empty else
};
```

`ScanOptions` is the **fully-resolved** input — the engine never
reads `settings.json` itself; GUI/CLI merge settings → options
(provider string `"malwarebazaar"|"virustotal"|"none"`, scope enum,
api key, toggles) before `Start`. Phases: **A** enumerate all three
sources; **B** per-item signature+hash (proc/svc only) then
`EvaluateItemHeuristics` + interim `FinalizeSuspicion` (score feeds
the SuspiciousOnly filter); **C** reputation — dedupe hashes, scope
filter, 4 workers + pacing, `ShouldStopBatch()` honored, findings
appended via `detection::AppendReputationFinding`, final
`FinalizeSuspicion`; **D** sort score-desc/name-asc, stamp times.
Cancel is checked between items and between HTTP calls; a cancelled
scan keeps partial items, `success=false`, `cancelled=true`.
`CollectLookupHashes(items, scope)` is a free function (pure,
unit-tested) that Phase C consumes.

## Settings — `settings_store.cc`

`%LOCALAPPDATA%/XER/ProcessTask/settings.json` (jsoncpp, via
`JsonCppHelper`):

```json
{"version":1,"provider":"malwarebazaar","virustotal_api_key":"",
 "reputation_scope":"auto","check_signatures":true,"hash_files":true,
 "use_reputation":true}
```

API key stored plaintext — it's a free read-only VT key; documented.
File absent ⇒ defaults, `LoadSettings` returns true. File present
but unparseable ⇒ defaults filled, returns false + `error_out`.
GUI edits persist on change; CLI reads the same file (env/CLI
override wins).

## GUI — `audit_widget.{h,cc}` (WIN32, exe-side)

`class AuditPanelWidget final : public XER::UI::Widgets::Widget`
in `XER::ProcessTask`. Window chrome follows `preview/main.cc`:
frameless + `AppTitleBar` + dark theme, ~1280×800, `SetFrameCallback`
→ `TickTree(0.1)` driving engine progress polling + control state.

```
┌─ AppTitleBar ─────────────────────────────────────────────────┐
├─ Toolbar row 1: [Scan] [Cancel*]  status Label (phase/progress)│
├─ Toolbar row 2: Provider Select │ Scope Select │ [x]Sign [x]Hash│
│   [x]Rep │ API-key TextInput │ [Export JSON…] [Export CSV…]    │
├─ Tabs: Processes | Installed programs | Services               │
│  ┌─ Table (per kind; columns differ) ────────────────────────┐ │
│  │ Processes: Name PID PPID Path Start Score Findings        │ │
│  │ Programs:  Name Version Publisher InstallDir Score Find.  │ │
│  │ Services:  Name DisplayName State StartType PID Path Find.│ │
│  └───────────────────────────────────────────────────────────┘ │
├─ Details (bottom split):                                       │
│  left  Textarea (read-only): all item fields + reputation     │
│  right Table: findings — Severity | Rule | Message            │
└─ Status bar: "N processes · M programs · K services · F flagged"│
```

*Cancel enabled only while `engine->IsRunning()`. Export buttons
use `GetSaveFileNameW` (node_graph_editor_widget.cc:196 pattern)
with `.json`/`.csv` filters, then `report_export` on the last
completed `ScanResult` (disabled until first scan completes).
Row click → details pane; tables rebuild from `ScanResult.items`
filtered by kind, sorted score-desc (engine pre-sorts; Table has no
sort — see Risks). `Table` cells are strings — score renders as
`"72"` and findings as `"3 (high)"`; severity colors live in the
details/findings tables' text (Table has no cell-color API).

## CLI — `main.cc`

```
process_task.exe                        GUI (Windows)
process_task.exe --scan                 headless scan → stdout summary
                                        + top suspicious rows
process_task.exe --scan --json <path>   also write JSON report
process_task.exe --scan --csv <path>    also write CSV report
process_task.exe --scan --no-reputation skip provider lookups
process_task.exe --scan --reputation-scope all|suspicious
process_task.exe --provider malwarebazaar|virustotal|none
process_task.exe --api-key <key>        VT key (else env/settings)
process_task.exe --no-signatures | --no-hash
process_task.exe --help
```

`--scan` prints progress to stderr, a per-kind count summary, then
the top ~50 rows by score (`FormatItemsTable`) with rule IDs; exit
code 0 on success, 1 on failure, 2 on cancel. Non-Windows: `--scan`
prints "not supported" and exits 1; GUI prints the preview-style
fallback message. argv re-parse via `CommandLineToArgvW`→UTF-8
(`preview/main.cc:65-76` pattern) so non-ASCII paths survive.

## Export formats — `report_export.cc`

**JSON** (`xer-process-task-report`, via jsoncpp):

```json
{"format":"xer-process-task-report","version":1,
 "tool":{"name":"process_task","version":"<exe semver or "dev">"},
 "scan":{"started_utc":"…","finished_utc":"…","hostname":"…",
         "options":{…ScanOptions…}},
 "summary":{"processes":0,"programs":0,"services":0,"items":0,
            "flagged":0,"by_severity":{"info":0,"low":0,"medium":0,
            "high":0,"critical":0}},
 "items":[{ /* full AuditItem: kind,name,pid,ppid,path,…,signature,
              signer,sha256,reputation:{…},findings:[{severity,rule,
              message}],suspicion_score,worst_severity */ }]}
```

`items` sorted score-desc. **CSV**: one row per item, RFC-4180
quoting, columns = union of fields (empty for non-applicable),
`findings` joined `";"` as `rule|message`, reputation flattened to
`rep_provider,rep_verdict,rep_malicious,rep_suspicious,
rep_undetected`.

## Build integration

`tools/process_task/CMakeLists.txt` (mirrors video_editor):

```cmake
file(GLOB PROCESS_TASK_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
list(FILTER PROCESS_TASK_CORE_SOURCES EXCLUDE REGEX "/(main|audit_widget)\\.cc$")

add_library(XER_ProcessTask_Core STATIC ${PROCESS_TASK_CORE_SOURCES})
target_include_directories(XER_ProcessTask_Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_link_libraries(XER_ProcessTask_Core PUBLIC
    XER_Network      # SyncRequest + OpenSSL::Crypto (transitive)
    XER_Helper       # string/path/env/jsoncpp helpers
    XER_Hex          # header-only hex
    jsoncpp_lib)
target_compile_features(XER_ProcessTask_Core PUBLIC cxx_std_23)
if(WIN32)
    target_link_libraries(XER_ProcessTask_Core PRIVATE wintrust advapi32)
endif()
# /W4 /WX- /O2 MSVC ; -Wall -Wextra -Wpedantic otherwise

add_executable(process_task main.cc)
target_include_directories(process_task PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(process_task PRIVATE
    XER_ProcessTask_Core XER_Flux_Core)
if(WIN32)
    target_sources(process_task PRIVATE audit_widget.cc)
    target_link_libraries(process_task PRIVATE XER_UI_Core)
endif()
set_target_properties(process_task PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
```

Root `CMakeLists.txt`: `add_subdirectory(tools/process_task)` next to
`tools/video_editor` (:476). `tests/CMakeLists.txt`: new
`XER_ProcessTask_Tests` exe + `add_test`, unconditional (non-Windows
stubs keep it buildable; enum tests self-skip via capability check).

## Testing / verification

`tests/process_task/` — `process_task_test_util.h`
(`XER_PT_CHECK`, `TempDir()`), `process_task_test_main.cc` (suite
runner calls, shared counter), suites:

- `process_task_detection_tests.cc` — every rule ID exercised on
  synthetic `AuditItem`s + `SystemContext` fixture (temp dirs under
  `%TEMP%`); score math, worst-severity, ordering. Pure — no Win32.
- `process_task_hash_tests.cc` — SHA-256 known-answer vectors
  ("abc"→`ba7816bf…`, empty→`e3b0c442…`, a temp file round-trip).
- `process_task_signature_tests.cc` — `CheckAuthenticode` on a temp
  `.txt` (expect Unsigned/Error, never Trusted) and, if present,
  `kernel32.dll`/`notepad.exe` (expect Trusted — self-skip if the
  file is absent; Windows always has them but the guard keeps the
  test honest on odd installs).
- `process_task_enum_tests.cc` — live enumeration smoke: processes
  non-empty and contain own PID; services non-empty (any Windows has
  services); programs ≥0 (assert fields sane when present);
  self-skips entirely when `Enumerate*` reports unsupported.
- `process_task_reputation_tests.cc` — `FakeTransport` returning
  canned MB/VT payloads: parse ok/not-found/error paths, VT
  401/404/429 handling, scope filtering, hash dedupe (engine-level:
  2 items same hash ⇒ 1 lookup). Optional live test self-skips when
  `SyncRequest` to mb-api fails fast (offline CI).
- `process_task_export_tests.cc` — JSON round-trip parse via
  jsoncpp (fields, findings, summary counts), CSV quoting/escaping,
  temp-dir output files.
- `process_task_engine_tests.cc` — `AuditEngine` headless run with
  reputation disabled + tiny scope: completes, items sorted,
  progress callbacks observed, cancel path (start→cancel→Cancelled).

Manual verification: `process_task.exe --help`, `--scan` (offline,
`--no-reputation`) prints sane counts; GUI launches, Scan populates
tabs, suspicious fixture visible; JSON/CSV open cleanly.

## Risks / open issues

- **Table widget gaps**: no per-row colors, no implemented sort
  (`Column.sortable` is declared, `table.cc` has no sort code),
  string cells only. v1 works around with score text + details pane;
  sorting could be added to `table.cc` later (it's `src/` — out of
  scope for read-only constraint; alternative: sort rows before
  `SetRows`, which we already do). Row tinting would need a Table
  API addition — deferred, flagged to user.
- **WinVerifyTrust quirks**: catalog-signed system files
  (KernelBase.dll etc.) may report `SignedUntrusted` without revocation
  checks on some systems; treat `SignedUntrusted` as "signed but
  chain incomplete" in UI copy, not "malicious". 32-bit vs 64-bit:
  tool is x64; SysWOW64 binaries of 32-bit processes resolve via
  `QueryFullProcessImageNameW` correctly (it returns the real path).
- **Protected processes**: OpenProcess fails for PPL (MsMpEng,
  csrss when non-elevated) → `path=""` + Info finding; document that
  elevation yields fuller data (no auto-elevate in v1 — Scan just
  notes reduced coverage in status bar when denials are frequent).
- **Reputation rate limits**: MB anonymous tier is friendly but not
  unlimited; VT free = 4/min. Dedup + pacing + scope defaults keep
  us well under; first-429 abort for VT is the safety valve.
- **Registry volume**: ~200-600 uninstall entries; enumeration is
  fast, but `UninstallString` paths may be `MsiExec.exe /I{guid}` —
  path heuristics apply only when the string looks like a path;
  MSI-only entries get name/publisher checks only (documented).
- **False positives**: unsigned portable apps in Downloads score 15
  (Medium) — acceptable for an auditor (it's a flag, not a verdict);
  the details pane shows exactly which rules fired so users can
  judge. Tunable weights are `constexpr` in `detection.cc`.

## What's left to the builder

- Exact heuristic weight tuning after a real-machine scan pass
  (weights are `constexpr` — expect one calibration commit).
- `service_dll` registry read edge cases (missing Parameters key,
  32-bit services on 64-bit — `WOW6432Node` service view is NOT a
  thing for `Services` keys; CurrentControlSet is shared).
- Whether `PT-NAME-ANOMALY` should also catch right-to-left override
  unicode in filenames (v1: ASCII checks only).
- GUI polish level: whether details pane earns a third column for
  raw JSON of the item; keyboard navigation in tables.
- `--scan` output format details (column set of `FormatItemsTable`).
- MalwareBazaar response field names — parse defensively, verify
  against a live response during implementation (can't be verified
  offline).
- Version string source for `tool.version` in export (CMake
  `PROJECT_VERSION` vs hardcoded "dev").

## Builder calls already made (flag if you disagree)

- Namespace `XER::ProcessTask`; flat `tools/process_task/` layout;
  `XER_ProcessTask_Core` static lib + thin `process_task` exe;
  `audit_widget.cc`/`main.cc` excluded from the core glob.
- One flat `AuditItem` struct (not per-kind variants).
- Signature+hash scope = processes + services only; programs are
  metadata+heuristics only in v1.
- Reputation default scope: MalwareBazaar = all unique candidate
  hashes; VirusTotal = suspicious-only (its 4/min free tier makes
  "all" impractical); `Auto` defers to provider.
- Reputation "Clean" emits no finding (only the export records it).
- No command line / process-user collection in v1 (reserved fields).
- No SQLite/catalog — ephemeral scans + file export (per decision
  doc's own lean).
- Settings at `%LOCALAPPDATA%/XER/ProcessTask/settings.json`; VT key
  precedence CLI > env `XER_PROCESS_TASK_API_KEY` > settings file.
- `wintrust` + `advapi32` linked PRIVATE into `XER_ProcessTask_Core`
  on WIN32 only; core compiles/stubs off-Windows.
- Seven implementation tasks (see plan) — enumeration, analysis,
  detection, reputation, engine/export, GUI/CLI are separate
  commits.
