# Live UI testing — driving the real GUI for verification

How agents verify GUI behavior on this tree: launch the real binary,
drive it with synthetic input, screenshot, check exit state, kill.
Written 2026-11-21.

## Why headless checks are not enough

`video_editor --smoke` exercises decode/store/export paths headlessly,
but entire failure classes only appear with a live window: CEF child
processes, GPU init, message-loop wiring, focus/activation bugs.
Motivating example (2026-11-21): driving video_editor.exe live, the
stderr log (`/tmp/ve_run.log`-style capture) showed the CEF GPU
process crash-looping — repeated
`GPU process exited unexpectedly: exit_code=-1073741511`
(0xC0000139, `STATUS_ENTRYPOINT_NOT_FOUND`) — invisible to `--smoke`
because the app stayed up and rendering; only the stderr log and the
missing browser panel gave it away.

## tools/scripts/ui_drive.ps1

Reusable driver distilled from the 2026-11-21 ad-hoc scripts. Three
verbs (functions when dot-sourced, `-Command` when run with `-File`):

```
powershell -File tools/scripts/ui_drive.ps1 -Command wait `
    -ProcessName video_editor -TimeoutSec 15
powershell -File tools/scripts/ui_drive.ps1 -Command click `
    -ProcessName video_editor -X 640 -Y 400
powershell -File tools/scripts/ui_drive.ps1 -Command shot `
    -ProcessName video_editor -OutFile C:/Temp/ve.png
```

- `click` posts WM_LBUTTONDOWN/UP via PostMessage at CLIENT coords —
  no focus steal, works on occluded windows.
- `shot` SetForegroundWindow + CopyFromScreen of the window rect to
  PNG. Needs an interactive desktop session; agents running headless
  cannot screenshot (expected limitation).
- Exit code 0 on success, 2 when the window/process isn't found —
  gate follow-up steps on it.

## Recommended verification loop

1. Build, then launch the exe detached with stderr captured:
   `video_editor.exe 2>/tmp/ve_run.log` (MSYS2) or redirect to a file
   under `%TEMP%`.
2. `wait` for the window (gives the app time to boot CEF/V8).
3. `click` the control under test — coords are CLIENT coords; get
   them from a `shot` screenshot first if unsure (the window's
   top-left is 0,0).
4. `shot` after the interaction; read the PNG back to confirm the
   visual state changed (a no-op click that "succeeded" is the most
   common false positive).
5. Check the process: still-alive is not enough — grep the stderr
   log for `ERROR`, `FATAL`, `exit_code=` crash loops (the
   gpu_process 0xC0000139 pattern above), and `[CRASH]` blocks from
   VeCrashTrace (see crash-debugging-windows.md).
6. Kill the process when done (`Stop-Process -Name video_editor`)
   — a running exe holds a lock that fails the next build's link
   step (see windows-msys2-gotchas.md).

## Caveats

- PostMessage clicks bypass the input queue's real-mouse side
  effects: hover states, focus changes, and drag routes that read
  `GetAsyncKeyState` may not fire. For pure button activation it is
  reliable; for drag/gesture verification, prefer asserting on
  observable app state (log lines, project files) plus screenshots.
- One process may own several windows (CEF native children,
  dialogs). `MainWindowHandle` picks the primary one — transient
  dialogs need EnumWindows-style enumeration, which the script
  deliberately does not attempt yet.
