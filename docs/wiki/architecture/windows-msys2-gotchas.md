# Windows / MSYS2 gotchas

Environment traps for the `windows-clang-msys2` toolchain (MSYS2
clang64, clang + libc++). Collected 2026-11-21; see
[build-and-test-infra.md](build-and-test-infra.md) for the build flow
and [static-link-state.md](static-link-state.md) for link layout.

## Running exes lock the build

A `video_editor.exe` left running — or an orphaned
`xer_engine_ui_render.exe`/`xer_browser_shell.exe`/CEF `--type=`
child that survived its parent — holds `libcef.dll`,
`chrome_elf.dll`, the staged skia DLLs **and the exe link outputs**
open. Symptoms: `copy_directory ... failed after retries`
(CopyDirRetry.cmake) and `ld.lld: failed to write output`.

**Handled automatically**: `Build.ps1` runs `Stop-LockingProcesses`
before clean/configure/build — it kills `video_editor`,
`xer_engine_ui_render`, `xer_browser_shell`, `XBrowser`, `XERVacUI`,
`Editor`, `XER_VST3_CEF` **only when the process image path is under
`<BinaryDir>\bin`** (a same-named exe elsewhere is never touched).
`-KeepRunning` opts out. CopyDirRetry's final FATAL_ERROR now also
names the suspect PIDs. Manual fallback:

```powershell
Get-Process video_editor,xer_engine_ui_render,xer_browser_shell,XBrowser,XERVacUI,Editor,XER_VST3_CEF -EA SilentlyContinue | Stop-Process -Force
```

## MSYS2 path translation

- Windows form `D:\Developers\XER3` ↔ MSYS2 form
  `/d/Developers/XER3`. Convert with `cygpath -w` (to Windows) /
  `cygpath -u` (to POSIX) / `cygpath -m` (mixed `D:/...`). CI uses
  `cygpath -m /` to resolve the MSYS2 root.
- **Native exes need Windows-style paths.** The MSYS2 shell converts
  path-looking *arguments* for non-MSYS2 binaries, but conversion is
  best-effort and does NOT apply inside env vars, JSON payloads, or
  config strings — and `MSYS2_ARG_CONV_EXCL` can suppress it
  entirely. Anything a native exe parses itself must carry `D:/` or
  `D:\`: e.g. the `--media-worker` subprocess
  (`tools/video_editor/media_worker_client.cc` spawns itself via
  `GetModuleFileNameW` + `CreateProcessW`) exchanges JSON-lines
  requests whose `path` fields are resolved with Win32/
  `std::filesystem` — a `/d/...` path there just fails to open.
- In CMake/script files prefer forward slashes (`D:/...`) — they
  survive both worlds; use `file(TO_NATIVE_PATH)` before handing a
  path to PowerShell `StartsWith` comparisons (process `Path` is
  backslash form).

## GUI-subsystem console contract

- `xer_app_win32(<target>)` (root `CMakeLists.txt:16-37`) compiles
  the per-target `app.rc` (icon + version) and adds `-mwindows` for
  every non-Debug config — double-click launches never flash a
  console; Debug keeps one.
- GUI-subsystem exes keep stdout working through inherited pipes
  (`video_editor --smoke | ...` fine). To print when launched from a
  terminal, `tools/video_editor/main.cc:707-733` borrows the parent
  console via `AttachConsole(ATTACH_PARENT_PROCESS)` + `freopen_s`
  to `CONOUT$`, but **only when stdout isn't already a pipe/file**
  (`GetFileType` check — v1 rebound unconditionally and swallowed
  piped output).
- **NEVER AttachConsole for `--media-worker` or CEF `--type=`
  children** — their stdout IS the IPC channel (JSON-lines worker
  protocol / CEF subprocess pipes). `main.cc:713-718` sets
  `pipe_mode` to skip the attach. Any new headless-subprocess mode
  must join that list.

## windres / .rc files

- `windres`/`llvm-windres` mangles **non-ASCII bytes inside string
  values** — keep `app.rc.in` `VALUE` strings pure ASCII (author
  string "Matvey Vladimirovich (Yoshi Arasaka & LXXV)" is ASCII; the
  CMake comment above `xer_app_win32` quoting Russian is fine
  because comments don't reach the resource). If localized strings
  are ever needed, verify the emitted resource bytes — don't trust
  the .rc source.

## Where tools live

- MSYS2 clang64 toolchain on this machine:
  `C:\tools\msys64\clang64\bin` (MSYS2: `/c/tools/msys64/clang64/bin`)
  — clang, lld, ninja, cmake, ccache, **ffmpeg.exe**, windres,
  llvm-symbolizer, all dep DLLs/implibs. `Build.ps1` resolves
  cmake/ninja from PATH first, then this dir (`$MsysClangBin`).
- CI runners resolve the MSYS2 root dynamically
  (`cygpath -m /` → `XER_MSYS2_ROOT`), so scripts must not hardcode
  `C:\tools` outside Build.ps1's documented fallback.
