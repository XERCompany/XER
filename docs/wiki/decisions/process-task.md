# Decision: ProcessTask — standalone process/service auditor

**Status:** APPROVED scope (2026-11-15). Third workstream alongside
VideoEditor 1C and toolchain migration.

## User rulings (2026-11-15)

- **Placement:** NEW standalone tool `tools/process_task/` (same
  pattern as `tools/video_editor`, `tools/preview`) — NOT a suite
  workspace mode, NOT inside src/content.
- **Detection v1:** heuristics + EXTERNAL reputation. Reputation
  provider must be pluggable: MalwareBazaar/abuse.ch (free hash
  lookup) as default, VirusTotal optional (needs user API key).
- **Format v1:** GUI table of processes/programs/services +
  suspiciousness flags + export JSON/CSV.

## Feature sketch (from user request)

- Full enumeration: running processes, installed programs, services.
- Suspicion scoring: programs masquerading as Windows system
  processes — e.g. `svchost.exe` outside System32, unsigned binary
  claiming system name, system-named exe in user/temp dirs, missing
  Authenticode on "system" paths, service binary path anomalies.
- Defensive security only — detection/reporting, no remediation in v1.

## Repo assets to reuse (verify in design phase)

- `src/content/process` — existing process enumeration APIs
- `src/security_flash`, `tools/SecurityFlashHello`,
  `ControllAccountsWindows` — Windows account/service precedents
- `src/content/network` — HTTP client for reputation lookups
- `XER_UI_Core` (Win32+D2D+DWrite) for the GUI table
- `tools/video_editor` — pattern for tools/ standalone app structure

## User follow-up answers (2026-11-15)

1. **UI table fix APPROVED** — colored/sortable rows in
   `src/ui/widgets/table.cc` is IN SCOPE (plan amended to add it;
   benefits all tools).
2. VT API key plaintext in settings.json — OK (env override exists).
3. MalwareBazaar defensive parsing + live verify at impl — OK.
4. No auto-elevate v1 — OK.
5. 7 tasks — OK.

## Open questions for design phase

- Service enumeration source (SCM API vs registry vs existing code)
- "Installed programs" = registry uninstall keys? Start-menu? both?
- Reputation: hash-only (SHA-256) vs hash+path+signature; offline
  mode behavior; rate limits
- Persistence: SQLite catalog like video_editor? Probably overkill —
  scan is ephemeral; export covers history
