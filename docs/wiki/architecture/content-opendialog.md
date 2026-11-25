# content/opendialog — native file pickers with embedded widgets

Written 2026-11-24, base commit `a50dddf` (branch
`feat/ui-kit-ve-shell`). Verified by a `windows-clang-msys2` build of
`XER_ContentOpenDialog` + `XER_OpenDialog_Smoke` (14/14 selftest
checks pass; interactive dialog runs are manual-only — see
"Verification" below). All paths relative to repo root.

## What it is

`src/content/opendialog/opendialog.{h,cc}` — Windows common-item-dialog
pickers behind a small UTF-8 C++ API, with **custom controls rendered
inside the system file dialog** (the Office/Adobe pattern: extra widgets
embedded in the explorer area) via `IFileDialogCustomize`, plus live
control events via `IFileDialogControlEvents`.

Platform story: **Windows-only for now.** On Linux/macOS the same
header compiles and every entry point returns a `Result` with
`confirmed == false` and `error` set, so call sites can be written once
and light up when a backend lands (GTK4/KDE portal, NSOpenPanel).

## API surface (`XER::Content::OpenDialog`)

- `OpenFileRead(Options)` — `IFileOpenDialog`
  (`FOS_FILEMUSTEXIST`/`FOS_PATHMUSTEXIST`, `FOS_ALLOWMULTISELECT`).
- `OpenFileWrite(Options)` — `IFileSaveDialog`
  (`FOS_OVERWRITEPROMPT`, `SetDefaultExtension` auto-append).
- `OpenDirectoryRead(Options)` — `IFileOpenDialog` + `FOS_PICKFOLDERS`.
- `OpenDirectoryWrite(Options)` — pick-or-create folder via
  `FOS_PICKFOLDERS | FOS_CREATEPROMPT`.
- `OpenFileReadAsync` / `OpenFileWriteAsync` / `OpenDirectoryReadAsync` /
  `OpenDirectoryWriteAsync` — `std::future<Result>` twins; the modal
  dialog runs on a worker thread (COM is initialised there).
- `Options`: `owner` (`void*` HWND — pass
  `XER::UI::Window::GetHWND()`, src/ui/core/window.h:156, for modal
  parenting), `title`, `filters` (or `ParseFilterSpec("Name|*.a;*.b|…")`),
  `default_directory`, `default_name`, `multi_select`,
  `file_must_exist`/`path_must_exist`, `overwrite_prompt`,
  `create_prompt`, `default_extension`, `custom`.
- `Result`: `confirmed`, `paths` (UTF-8), `error`,
  `used_legacy_fallback`, `custom` (`CustomResult`).

## Embedded widgets ("виджеты в проводнике")

`Options::custom` (`CustomControls`) opts into three controls grouped
under a labelled visual group ("XER options") at the bottom of the
dialog:

| Control        | IFileDialogCustomize call           | Reads back via                 |
|----------------|-------------------------------------|--------------------------------|
| check button   | `AddCheckButton` ("Open read-only") | `GetCheckButtonState` → `custom.checked` |
| combo box      | `AddText` + `AddComboBox` + `AddControlItem` ("Encoding:") | `GetSelectedControlItem` → `custom.combo_index`/`combo_text` |
| edit box       | `AddText` + `AddEditBox` ("Note:")  | `GetEditBoxText` → `custom.edit_text` |

The event extension point is wired too: `DialogControlEvents`
implements `IFileDialogEvents` + `IFileDialogControlEvents`, is advised
through `IFileDialog::Advise` (NOT `IFileDialogCustomize` — a trap:
`Advise`/`Unadvise` live on `IFileDialog`; the dialog QIs the sink for
the control-events interface), and `OnCheckButtonToggled` greys out the
note edit box when "read-only" is checked — live inside the running
dialog.

## Fallback path

If `CoCreateInstance(CLSID_FileOpenDialog/FileSaveDialog)` fails, the
module silently falls back to `GetOpenFileNameW`/`GetSaveFileNameW`/
`SHBrowseForFolderW` (precedent: `tools/video_editor/ui2/ve_app.cc:2599`,
`tools/ide_xejso/ide_app.cc:580`). Legacy dialogs cannot host the custom
widgets, so the fallback sets `used_legacy_fallback` and leaves
`CustomResult` zeroed. Legacy was NOT chosen as primary because
`IFileDialogCustomize` cannot extend the comdlg32 pickers.

## Build

- `src/content/opendialog/CMakeLists.txt` → `XER_ContentOpenDialog`
  (static lib, same pattern as `passwordgen`/`clipboard`), PUBLIC-links
  `XER_Helper` (`Utf8ToWide`/`WideToUtf8`), PRIVATE `ole32 shell32
  comdlg32 uuid` on Windows.
- `add_subdirectory(src/content/opendialog)` appended in the
  "Content subsystems" block of root `CMakeLists.txt` (after
  `src/helper`, which it links).
- `tests/opendialog_smoke.cc` → `XER_OpenDialog_Smoke`, registered in
  `tests/CMakeLists.txt` + `add_test`.

## src/ui seam

`Options::owner` is the modal-parenting seam — a widget-kit window
passes `window->GetHWND()` as `void*`. The existing
`src/ui/widgets/dialog.{h,cc}` `Widgets::Dialog` is an in-app overlay
(scrim + glass panel), not an OS file picker, so nothing was wired
behind it. Candidate follow-up: swap the three `GetOpenFileNameW` call
sites in `src/ui/widgets/` (`video_preview_widget.cc:43`,
`projects_widget.cc:55`, `node_graph_editor_widget.cc:198`) onto this
API.

## Verification

- Headless (CI-safe): `XER_OpenDialog_Smoke` runs `RunSelftest` —
  filter-spec parsing, the double-NUL legacy filter buffer, option
  validation, `Result` zero-init. 14/14 PASS on
  `windows-clang-msys2`; registered as a ctest.
- Interactive (manual, needs a desktop session — shell dialogs cannot
  run in CI):
  `XER_OpenDialog_Smoke --dlg-open|--dlg-save|--dlg-dir|--dlg-dir-write`
  — each embeds the three custom controls so their rendering, the
  check→edit enable coupling, and value round-trip can be eyeballed.
