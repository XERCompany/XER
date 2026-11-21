# Extension Platform: .xexrj / .xsjo / .xejslib / ve.core.h

Facts verified as of 2026-11-21, at the tree containing smoke check
`ve_core_v3` (38 checks total). All claims dated.

## Pipeline

```
extensions_src/<name>/
  native/*.cc          → ztzmake.exe (in-process clang::CompilerInstance,
                          EmitObjAction → COFF x86_64-w64-windows-gnu,
                          no subprocess) → mobjective/<stem>.xsjo
  scripts/*.js         → archive member (V8 in-process via ext_host)
  static/*.html        → archive member (CEF xer-ext://<name>/...)
  manifest.toml        → [native] sources=[...] deps=[...]
        │
        ▼ VE.ExtensionsRuntime.exe (worker_pipe JSON-lines IPC)
   orc::LLJIT, SymbolMap(ve_* → host funcs), ve_ext_entry(ctx)
```

- `.xsjo` wire format: `XSJ1` magic + triple + COFF object. Written by
  `tools/tz/ztzmake.cc`; loaded by `tools/video_editor/extensions/xsjo_jit.cc`.
- `.xejslib` = reusable native library: `XJSL` header + JSON metadata +
  embedded `XJSH` header pack + length-prefixed `.xsjo` modules.
  Written by `tools/tz/xejslib.cc`; built by `tools/tz/xsjobuildx.cc`
  (`XSJOBuildX.exe <srcdir> -o lib.xejslib`).
- Consumption: `ztzmake --dep lib.xejslib`, `[native] deps=[...]`, or
  auto-scan of `libs/*.xejslib`. Modules splice into the archive as
  `mobjective/lib_<stem>.xsjo`.

## JIT linking trap (fixed 2026-11-21)

Each `.xsjo` originally got its own `orc::JITDylib` → cross-module
symbols never resolved. `XsjoJit::LinkModules` (xsjo_jit.cc) now meshes
dependency modules; `HandleExtLoad` (ext_runtime_server.cc) links all
extension modules after load. Verified by `ve_demo_gain` calling a
mangled C++ namespace function + rodata across modules.

## ve.core.h API generations

- v1: log/status/project_info/add_clip (one-way, fire-and-forget).
- v2 (2026-11-21): reverse anonymous-pipe bridge env vars
  `XER_VE_EXT_HOST_REQ` / `XER_VE_EXT_HOST_RSP`; `ve_call` blocks the
  JIT thread → dispatched on editor UI thread → reply. `ve_add_clip`
  real.
- v3 (2026-11-21): ~60 services behind `DispatchExtHostCall`
  (`ui/video_editor_app.cc` ~L5210): `ve.services`, `app.info`,
  `project.*` (new/open/save/settings/markers/work_area),
  `timeline.*` (tracks/clips/transitions incl. slip/slide/nudge/
  ripple_delete), `transport.*` (play/seek/shuttle/loop/quality),
  `selection.*`, `media.*`, `fx.*`, `export.*`, `ui.*`, `edit.*`.
  All mutations route through `history_` (undoable). `app.quit`
  deliberately absent. v2 aliases preserved.
- JS parity: `xer_ext.ve(name,args)` / `.veCall` in `ext_host.cc` calls
  the SAME dispatch table via a `"ve_call"` action → synchronous JSON.
  `xer_ext.nativeCall(id,fn,args)` is fire-and-forget only — a sync
  version would deadlock the host_call pump (single-threaded proto).

## IDE

`tools/ide_xejso/` → `IDEXEJSO.exe` ("Extension IDE - XER Creative
Suite"): project scaffold/tree/save + Build via sibling `ztzmake.exe`
process + Test via `ExtRuntimePipe` to `VE.ExtensionsRuntime.exe` +
"Open in Editor" deploys `.xexrj` → `bin/assets/extensions/` and
launches `video_editor.exe`. `--selftest` exercises the whole chain
(scaffold → build → load → ve_ext_entry → events → probe → unload);
exit 0 on success — CI-usable headless check.

2026-11-21 (v2): the IDE grew an authoring surface —
`code_editor_widget` (custom Widget: tokenizer-based syntax
highlighting for C++/JS/TOML/JSON, line gutter, mouse caret+drag,
search-match overlays), `ide_format` (in-process
`clang::format::reformat` via the monolithic `clang-cpp`/`LLVM` link —
NO clang-format.exe subprocess; JS/TOML fall back to a builtin
idempotent beautifier), file tabs with dirty dots + autosave-on-close,
Ctrl+F find bar, clickable build diagnostics (file:line jump +
Jump-to-Next-Error), `ve.*` API reference/snippets sourced from the
live `VeServiceNames()` table, and `IdePreviewPanel` — a lazy
`file://` CEF surface (disable_gpu, multi_threaded loop — no pump)
fed by `IdeFileWatcher` (ReadDirectoryChangesW + 300ms debounce,
drained in Widget::Tick) so editing any .html/.css/.js sibling
auto-reloads the preview with a `__xer_r=N` cache-busting re-nav.
Verified live: external `sed` of static/index.html re-rendered the
panel in ~2s. `IdePreviewSelftest` (watcher round trip + headless CEF
create) is step 7 of `--selftest`.

2026-11-21 (v2.1, editing UX): the editor gained full IDE-grade text
editing — Html/Css languages (`.html/.htm/.xml/.xhtml/.svg/.css`;
`<script>` delegates to the JS tokenizer, `<style>` to CSS, per-line
embedded state), auto-indent on Enter (+1 level after `{([` or an
open tag, `{|}` 3-line split, per-file tab-vs-4-space detection via
`DetectIndentUnit`), Tab/Shift+Tab line indent/dedent (required a
`widget_tree.cc` fix: Tab KeyDown is offered to the focused widget
before `MoveFocus`), auto-pairs `()[]{}""'` with overtype/pair-delete/
selection-wrap (skipped inside Comment/String tokens via
`TokenKindAtCaret`), draggable 9px overlay scrollbars on both axes +
caret-follows-X, double/triple-click word/line select, and Ctrl+/
comment toggle (`KeyCode::Slash` + `VK_OEM_2` added to
widget.h/input_translator.cc; `//`/`#`/`/* */`/`<!-- -->` per
language). Selftest step `editor-ux` covers the headless helpers
(`AutoIndentForNewline`, `PlanPairInput`, `PlanBackspace`, tokenizers).

2026-11-21 (v2.2, readability): root cause of "code almost
invisible" was `TextStyle` having no family concept — every DrawText
rendered Segoe UI. `TextStyle::monospace` added
(src/ui/rendering/renderer.h); honored in Direct2D `CreateTextFormat`
(Consolas + `FormatKey`), GDI `GdiText::FontFor` (FIXED_PITCH, tuple
cache key), Vulkan glyph cache, and the render_stream wire format
(u32 style flags bit0). Editor: code font `font_body + 1.5f` mono,
Plain falls back to VS Code #D4D4D4 when theme foreground lum < 0.72,
hover-alpha current line, accent caret, `ScrollBarWidget`-style pill
scrollbars, VS Code-style tab separators + hover-only ×. ABI note:
TextStyle is a plain struct crossing DLL boundaries — the flag rides
the render-stream wire so remote `xer_engine_ui_render` workers honor
it too. Hard rule reaffirmed: no
third-party processes — formatting, watching, and preview are all
in-process C++ APIs; only XER sibling tools (ztzmake,
VE.ExtensionsRuntime, xer_browser_shell via CEF) may spawn.

## CEF static pages

`xer-ext://<ext-id>/<path>` serves `.xexrj` static members through
`src/cef/cef_ext_scheme.cc` (ExtSchemeProvider bridge — needed because
registry lives in the executable's static lib copy while CEF's handler
runs in YA.VideoEditor-Core.dll, see static-link-state.md).
`disable_gpu=true` for embedded panels — the local CEF build's GPU
child dies silently during hw-init (0xC0000139 stale-import incident).

## Smoke coverage (as of 2026-11-21)

`ext_xsjo`, `ext_runtime`, `ext_bridge`, `ext_xejslib`,
`extensions_panel`, `ve_core_v3` (service table self-consistency +
describe layer + wire-name round trip + real ExtRuntimePipe
host_call round trip).

## Known v1/v3 limits

- `ve_call` has no timeout; a misbehaving extension can wedge its own
  JIT thread (worker kill is the backstop).
- `ve_add_clip` is fire-and-forget (no clip_id returned).
- JS→native (`nativeCall`) async-only, by design.
- IDEXEJSO v2 (supersedes the v1 Textarea note): build/test still run
  synchronously on the UI thread; the editor buffer does not yet
  auto-reload external on-disk changes (the CEF preview does);
  lexical (not semantic) highlighting — JS regex literals and `${}`
  inside template literals are not tokenized; GUI is Windows-only
  (`_WIN32` paths), headless tokenizer/formatter work everywhere.
- JS `xer_ext.ve()` can't be exercised headless — needs the running
  editor; smoke proves the identical dispatcher on the native side.
