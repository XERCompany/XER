# Crash debugging on Windows (MSYS2 clang64)

How to debug a native crash in this tree. Written 2026-11-21.

## No interactive debugger

The clang64 toolchain as installed (see the CI pacman list in
`.github/workflows/ci.yml` and `C:\tools\msys64\clang64\bin`) ships
**no gdb/lldb** — `gdb.exe`/`lldb.exe` are absent. Postmortem and
in-process tracing are the tools we have:

- `llvm-symbolizer.exe`, `llvm-addr2line.exe`, `llvm-objdump.exe`
  ARE present — use them on captured addresses/RVAs.
- The exes carry DWARF debug info in Debug/RelWithDebInfo builds;
  Release has limited info but symbol names still resolve.

## VeCrashTrace — in-process SEH reporter

`tools/video_editor/main.cc:653-694` (`VeCrashTrace`, installed via
`SetUnhandledExceptionFilter` at `main.cc:698`) prints, for a crash
on ANY thread:

```
[CRASH] code=0x%08lx addr=%p
[CRASH] module=<path> rva=<offset>
  #0 <module>!<symbol>+0x<disp>
  ...
```

module path + RVA + a dbghelp (`SymFromAddr`) symbolized backtrace —
the worker-thread decoder/mixer crashes that otherwise surface only
as an abrupt exit become diagnosable. A near-identical copy lives in
`tests/ui/test_main.cc:10-50` (`CrashTrace`) so the test harness gets
the same output. Keep the two in sync if you change one.

## Symbolizing RVAs manually

When a crash report gives you `module=... rva=0xNNNN` but no symbols
(Release build, or dbghelp couldn't resolve):

```bash
/c/tools/msys64/clang64/bin/llvm-symbolizer --obj=bin/video_editor.exe 0xNNNN
/c/tools/msys64/clang64/bin/llvm-addr2line -e bin/video_editor.exe -f -C 0xNNNN
```

Pass the **RVA** (the `[CRASH] module=... rva=` value), not the
absolute address — image base varies per run (ASLR).

## src/app/crash_handler.cc — dormant minidump implementation

`src/app/crash_handler.cc` is a fuller reporter: dbghelp **minidump**
+ text log + session state + single-shot recursion guard +
auto-restart (`--crash-depth=N`, max 2) + `crashes/pending.txt`
handoff to the new instance. It compiles into `XER_App` (globbed) but
`XER::App::Crash::Install()` is **never called** and `video_editor`
doesn't link `XER_App` — dead wiring today. If enabled, it would
supersede VeCrashTrace; until then it's reference material. (Separate
legacy `src/crash/crash_handler.cc` is a POSIX-signal variant used by
`src/error_handler/err_capture.cc` — different class entirely.)

## 0xC0000374 — heap corruption bypasses SEH filters

`STATUS_HEAP_CORRUPTION` is raised by the Windows heap via
**fast-fail** (`__fastfail`/RtlpLogHeapFailure), which terminates the
process WITHOUT running `SetUnhandledExceptionFilter` handlers —
VeCrashTrace prints nothing and the app just vanishes (seen while
debugging the mp3 cover-art crash). Tells:

- silent exit with no `[CRASH]` block, or code `0xC0000374`
  / `0xC0000409` (`STATUS_STACK_BUFFER_OVERRUN`, also fast-fail)
  in the exit status / Event Viewer → Windows Error Reporting.
- Response: reproduce under Application Verifier / `gflags /p
  /enable video_editor.exe /full` (pageheap makes the corruption
  fault at the write, inside SEH again → VeCrashTrace fires), or a
  `-fsanitize=address` build. Suspect buffer overruns in
  codec/parser code (ffmpeg-adjacent paths, tag/art parsing).

## Practical checklist

1. Re-run the exe from a console (Debug build keeps its own; Release
   reattaches via AttachConsole — see
   [windows-msys2-gotchas.md](windows-msys2-gotchas.md)) and capture
   stderr.
2. Note `code=` + `rva=` → symbolize offline.
3. Silent death → assume fast-fail; check Event Viewer for the real
   code, then pageheap/ASan.
4. `video_editor --smoke` exercises decode/store/export paths
   headlessly — often the fastest reproducer.
