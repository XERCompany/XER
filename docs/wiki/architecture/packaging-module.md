# Packaging module — `src/packaging` app bundler

Written 2026-11-24 on top of base commit `a50dddf`
(`feat/ui-kit-ve-shell` branch). Verified on preset
`windows-clang-msys2` the same day — build output, manifest and
screenshot evidence in this page.

## What it is

`src/packaging/` packs a distributable Windows bundle of an app:
the exe + its resolved DLL import closure + assets + an install
manifest. It exists as a **UI-free static lib** (`xer_packaging`, so
any host — CLI, GUI, tests, future JS module — can drive it) and a
**`xer_packager` tool** that is both a src/ui GUI and a headless CLI.

## API surface (`namespace XER::Packaging`)

| File | API |
|---|---|
| `pe_imports.h/.cc` | `ReadPeImports(path, PeImportInfo*, error, warnings)` — dependency-free PE32/PE32+ parser for data dir 1 (import) + dir 13 (delay-import, both RVA and legacy absolute-VA name encodings). |
| `dependency_scanner.h/.cc` | `ScanDependencies(exe, search_dirs, DependencyScanResult*, error)` — in-process port of `tools/pe_deps`: BFS over import+delay-import tables, resolves each name in exe-dir first then each search dir, transitive on hits. Result: sorted `dlls` (path, origin, delay flag), `unresolved` (OS/api-ms names), `warnings`. |
| `packager.h/.cc` | `Packager::Pack(spec, result, error, progress, log)` — validate → scan → copy into `output/<name>-<ver>-windows-x64/` → zlib CRC32 + sizes → `manifest.json` (`format: "xer-package/1"`) → optional `.zip` via `XER_Archive`. Also `Packager::DefaultSearchDirs(exe)` (exe dir + every `out/build/*/bin` under the repo the exe lives in) and `SanitizeName`. |
| `package_spec.h` | `PackageSpec`, `PackageResult`, `PackageFileEntry` (path/role/size/crc32), `DependencyScanResult`, `ResolvedDependency`, `PackageFormat` (Staging/Zip), `DepOrigin`. |

Error-reporting convention follows `XER::Content::Archive`:
`bool f(..., Out* out, std::string* error)` static methods, callbacks
for progress/log.

### Manifest schema (xer-package/1)

```json
{"format","name","version","entry_point","platform","created_utc",
 "icon"?, "files":[{"path","role","size","crc32"}],
 "unresolved_imports":[...]}
```

Roles: `entry_point | dependency | asset | icon | manifest`. The
`unresolved_imports` list is informational — OS-provided names
(KERNEL32.dll, api-ms-*, msvcrt, d3d11, …) plus genuinely missing
ones an installer may want to flag.

## The `xer_packager` tool

```
xer_packager                        GUI
xer_packager pack --exe a.exe --out dir [--name --version --icon
                  --add --search --zip]
xer_packager scan --exe a.exe [--search dir]
xer_packager screenshot --out p.png [--exe a.exe] [--size WxH]
```

`xer_packager <existing-path>` also opens the GUI prefilled.

### CPU render only — hard requirement honored

`ui/main.cc` pins the renderer instead of trusting the auto chain:

```cpp
app.SetRenderWorkerCount(0);           // no render-worker processes
window->SetRenderer(CreateRenderer(
    RendererBackend::kSoftware,        // GDI BGRA rasterizer
    window->GetHWND()));               // never d2d/d3d11/vulkan/ANGLE
```

No `XER_UI_RENDERER` env dependency, no GPU device, no
`xer_engine_ui_render.exe` pool.

### UI composition (`ui/packager_widget.cc`)

Everything is stock `src/ui` widgets — no custom `Surface`/paint
callbacks: `AppTitleBar` (+Pack action pill), `Label`, `TextInput`
(exe/output/name/version/extras/icon), `Button`
(browse/scan/pack; `GetOpenFileNameW`/`SHBrowseForFolderW` pickers
borrowed from `tools/preview` + `tools/ide_xejso`), `Checkbox` (zip),
`Table` (resolved deps with sizes), `Progress`, `MessageScroller`
of `Label` rows for the log pane. The widget does manual row layout
in `Layout()` like `Form` does.

Scan and pack run on a `std::async` worker; progress/log callbacks
marshal through a mutex-guarded deque drained in `Tick()` — file I/O
never blocks the UI thread (the sync-on-UI-thread trap from
`2026-11-20-gap-analysis`).

## CMake

`src/packaging/CMakeLists.txt` defines `xer_packaging` (STATIC;
PUBLIC `XER_JsonCpp` for the manifest, `ZLIB::ZLIB` for crc32; PRIVATE
`XER_Archive` when present → `XER_PACKAGING_HAS_ARCHIVE`) and
`xer_packager` (WIN32 + `XER_UI_Core` only; `RUNTIME_OUTPUT_DIRECTORY
bin`; `xer_stage_runtime_dlls(TARGET ...)` computed staging).

Root `CMakeLists.txt` (appended, no reordering):
`add_subdirectory(src/packaging)` after `add_subdirectory(tools/
ide_xejso)` — after `tools/pe_deps` so TARGET-mode staging exists —
plus `xer_packager` appended to the `xer_link_msys2_static` and
`xer_app_win32` lists (icon.rc + GUI subsystem on non-Debug; CLI
stdout still works through pipes, same as `video_editor`).

## Verified 2026-11-24

- `xer_packager scan --exe xer_packager.exe`: **42 resolved, 52
  OS/unresolved** — matches the `xer_pe_deps` POST_BUILD list
  exactly, delay-imports (ggml-1, vulkan-1, mkl_rt.3, libwhisper-1,
  libshaderc_shared) flagged.
- `xer_packager pack --exe xer_packager.exe --name XERPackager
  --version 1.0.0 --icon icon.ico --zip` →
  `out/package-test/XERPackager-1.0.0-windows-x64/` = exe + 42 dlls +
  icon.ico + manifest.json (45 entries), and
  `XERPackager-1.0.0-windows-x64.zip` (45 entries, ~117 MB —
  libskia.dll dominates). The staged copy re-scans itself
  successfully from the staging dir (closed dependency loop).
- UI screenshot (headless SoftwareRenderer, real scan data):
  `out/package-test/packager-ui.png`.

## Bug fixed on the way

`src/content/archive/archive_manager.cc`: `RewriteArchive` picked the
output format from the temp name `archive_path + ".xer-tmp"` —
`archive_write_set_format_filter_by_ext` returned ARCHIVE_FATAL and
poisoned the writer, so `AddFiles`/`Create` could never create a new
archive on this libarchive (3.8.11). Now `OpenWriter(path,
format_path, error)` derives the format from the real name while
writing the temp file. Verified: `xer_packager pack --zip` produces
a 45-entry zip.

## What a real installer-maker still needs

The staging tree + manifest is the payload an installer consumes;
missing pieces for a shipping installer:

- **Launcher/stub**: NSIS `.nsi` or Inno `.iss` script driving
  copy-to-Program-Files, shortcuts, uninstaller registry entries;
  or a self-extracting stub (7z SFX / appended-archive reader) if we
  stay self-hosted.
- **OS-level prereqs**: `unresolved_imports` are all system-provided
  for this exe (api-ms-*/ucrt arrive with Windows 10+), but a real
  installer should map them to redist checks where needed.
- **dlopen-only deps**: PE imports don't see `LoadLibrary` — ggml
  cpu-backend DLLs (ggml-cpu-*.dll), onnxruntime_providers_*, plugins.
  `extra_dirs`/`--add` covers them; a manifest "runtime-loaded" hint
  list would help.
- **Icon embedding**: the .ico is copied beside the exe; baking it
  into the exe needs the `.rc`/windres step (`xer_app_win32` already
  does it for our own targets — repacking a foreign exe doesn't).
- **Code signing** (signtool / osslsigncode) — installer + exe both.
- **Version/resource metadata** for foreign exes, file-version checks
  on upgrade, install-location + permission handling, delta updates,
  and payload compression better than zip-store if size matters
  (7z/xz via libarchive is one flag away).
