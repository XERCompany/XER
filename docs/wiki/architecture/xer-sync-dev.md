# xer_sync_dev — `.xersync` dependency-manifest utility

Date: 2026-11-25. Base commit: `d47f023` (branch `feat/ui-kit-ve-shell`).

`tools/xer_sync_dev` builds `xer_sync_dev.exe` — a dev utility that
operates on `third_party/3RD.xersyunc` (the literal filename; the
format is `.xersync`, a `-- format: json` comment line + JSON). It
implements the manifest's `automatic_*` policy: per-dep status, git
clone/fetch + binary-tarball staging, libclang-driven cmake patch
generation, static verification and a Build.ps1-friendly `auto` mode.

## 2026-11-26 update (as of `b7414b2`)

- **Manifest 16 → 165 deps**: `dependencies-from-github` 137 +
  `dependencies-opensource` 28 — the full `msys2-exit-plan.md`
  inventory: complete FFmpeg codec tree, glib/gnutls stacks, prebuilt
  toolchain routes (llvm-mingw ucrt `20261117`, pkg-config-lite, BtbN
  ffmpeg-builds, NVIDIA cuda/cudnn redist, MKL wheels, fftw/sqlite/
  boost). 48 deps carry `notes` flagging them 'hard' (ffmpeg autotools,
  glib stack, skia, v8, gnutls, oiio, opencv) with a per-dep path.
- **New schema fields**: `ref` pins a git dep (clone lands on the ref,
  drift re-pins, pinned deps skip update counting — used for
  pthreadpool `a56dcd79`, fxdiv `63058ef`, fp16 `4dfe081`,
  dlfcn-win32 `v1.4.2`); `notes` free-text; `build_mode` (root +
  per-dep via `EffectiveBuildMode`) — `cmake-gen` emits
  `BUILD_SHARED_LIBS OFF` scoping and `STATIC` source-list targets.
- **New flags**: `addfromgit --ref <sha>`; `mirrors[]` on binary deps
  (cef spotifycdn timeouts); `extract_map` remaps archive roots
  (onnxruntime nupkg → `win-x64/` + `include/`); `--cacert` + CA-bundle
  probing (libcurl cert failures).
- **onnxruntime source**: upstream github repo 404s — the working
  channel is NuGet v3 flat-container `microsoft.ml.onnxruntime`.
- **Restore-after-wipe lessons**: unpatched upstream pthreadpool fails
  on MinGW (`<alloca.h>` missing; `alloca` macro predefined by
  `malloc.h`) — the in-tree patch at `third_party/pthreadpool/src/
  pthreads.c:77` is load-bearing, keep it. Newer CEF dists put real
  runtime handlers under `libcef_dll/{cpptoc,ctocpp}/test/` — excluding
  `/test/` from the wrapper glob produced undefined-symbol links
  (`CefInputHandlerCppToC`, `CefCaptureBrowserWindowCallbackCppToC`);
  filter now only drops `_unittest|cef_dump_without_crashing`.
- **Known unverified URLs**: several clone URLs are canonical-upstream
  guesses (bitbucket x265, gitlab.xiph mirrors, git.libssh.org) — first
  full `sync` will surface bad ones. `w64devkit` is a 7z SFX —
  ArchiveManager compat untested. `ffmpeg-builds` latest-release asset
  name may drift.

## Subcommands

| command | what it does |
|---|---|
| `status <manifest>` | presence, upstream revision (git describe / `vendored@<sha>` for subtree-vendored dirs), last-check staleness vs `automatic_update_interval_days`, pending actions. No network. |
| `sync <manifest>` | `git clone --depth 1` missing git deps, `git fetch` + `rev-list --count HEAD..FETCH_HEAD` on stale present deps, libcurl download + `XER_Archive` extract for `dependencies-opensource` tarballs. Honors `--dep`, `--check-only` (no network/no writes), `--force`. |
| `cmake-gen <manifest>` | analyze each dep and emit `xer-<name>.cmake` **inside the dep dir** (a patch — upstream files are never modified), declaring target `XER-<name>`. |
| `verify <manifest>` | static check of generated patches: paren balance, command whitelist, `${CMAKE_CURRENT_LIST_DIR}` path existence, `add_subdirectory` target has `CMakeLists.txt`. |
| `auto <manifest>` | the `automatic_*` policy: cheap check-on-start report, interval-gated update pass (clones only with `--fetch`), then `cmake-gen` for every present dep when `automatic_create_patch_for_cmake_all_dependencies` is set. This is what Build.ps1/other tooling should call. |
| `selftest` | fully offline end-to-end run on a synthetic dep tree in `%TEMP%` (30 checks: manifest parse, status, cmake-gen for a static lib + a header-only dep, inter-dep edge detection, verify, state round-trip). |

## Key semantics

- **State file**: `third_party/.xersync-state.json` — per-dep
  `last_check` (ISO-8601 UTC), `head`, `version`, `applied_version`
  (`<prefix><rev>`), `pending_updates`, plus `last_auto_run` for the
  `auto` interval gate.
- **Applied version**: `git describe --tags --always` for real clones
  (`XER-2da0056`); the outer repo's last-touch commit for vendored
  subtrees (`vendored@8cb3f4d` → `XER-8cb3f4d`); the tarball's first
  `X.Y[.Z]` run for binary deps (`XER-158.0.6` for cef).
- **Repo detection**: a dep dir counts as its own repo only when
  `git rev-parse --show-toplevel` equals the dir — otherwise `git`
  would detect the outer XER worktree and report the XER sha as the
  dep's HEAD.
- **output_directory resolution**: relative to the manifest dir,
  except when its basename equals the manifest's own directory name —
  `third_party/3RD.xersyunc` + `"output_directory": "third_party"`
  means *the* `third_party/`, not `third_party/third_party`.

## clang API verdict

**libclang is available and used.** The MSYS2 clang64 toolchain
(`C:/tools/msys64/clang64`, LLVM/Clang 23.1.5) ships `clang-c/Index.h`,
`libclang.dll(.a)` and full cmake packages — no vendored LLVM needed
(`XER_BUILD_LLVM` stays OFF). `find_package(Clang CONFIG QUIET)`
exposes the imported `libclang` target; we link the shared C API.

Two repo-specific details matter:

- The MINGW `find_package` macro's `xer_retarget_imported_to_static`
  hook repoints `libclang`'s implib at `libclang.a` — we restore
  `IMPORTED_IMPLIB*` to `libclang.dll.a` in the CMakeLists block,
  because the static archive would drag the whole static clang/LLVM
  stack into the exe.
- Runtime DLLs (`libclang.dll`, `libLLVM-23.dll`, `libc++.dll`) are
  staged into `bin/` by `xer_stage_runtime_dlls(TARGET xer_sync_dev)`
  via `xer_pe_deps` — that's why the target block sits *after*
  `add_subdirectory(tools/pe_deps)` in root CMakeLists.

What the clang pass does (`analyzer.cc`, gated on
`XER_SYNC_DEV_HAVE_LIBCLANG`, fallback = textual `#include` scan):

- Parses each public header (cap 48) as a TU with
  `CXTranslationUnit_DetailedPreprocessingRecord` and harvests
  `clang_getInclusions` → resolved include graph → internal /
  inter-dep / system classification. `-I` args include every dep's
  candidate include dirs so cross-dep includes genuinely resolve.
- Real results on this tree: `libxml2 → dlfcn-win32 + zlib`,
  `re2 → abseil-cpp`, `pthreadpool → fxdiv`.
- Traps fixed along the way: Windows path case/8.3 canonicalization
  for `UnderDir`, self-edges (`libuv` matching its own `uv.h`), and
  standard-library spellings (`<string.h>` resolving into libxml2's
  *private* `src/string.h` — a `StdHeaders()` list now classifies
  C/C++ std names as system before dep probing; POSIX names like
  `dlfcn.h` are intentionally not in the list).

A textual scan always runs as well: it covers sources (libclang only
probes headers), extracts the macro-guard vocabulary for the patch
header comment, and is the documented fallback when libclang is
absent (`XER_SYNC_DEV_HAVE_LIBCLANG=0` — tool still builds).

## Generated patch shapes

- upstream `CMakeLists.txt` → thin wrapper: `BUILD_TESTING OFF` scoped
  override + `add_subdirectory(... EXCLUDE_FROM_ALL)` + `XER-<name>
  ALIAS <first upstream lib target>` (candidates regex'd out of the
  upstream file). See `third_party/zlib/xer-zlib.cmake`,
  `third_party/re2/xer-re2.cmake`.
- header-only (fp16, fxdiv) → `INTERFACE` target + include dirs.
- sources, no build files (libuv, pthreadpool, dlfcn-win32) → `STATIC`
  target with platform/test-filtered file list + `PUBLIC` include dirs
  + `if(TARGET XER-<dep>)` inter-dep links. Filters: skip dirs
  (`test/tests/examples/benchmarks/contrib/tools/...`), stem tokens
  (`*test*, *fuzz*, *bench*, *example*`, `shim`/`mock`/`fake`
  alternate-backend markers — pthreadpool's `src/shim.c` is the
  Emscripten backend upstream never compiles natively), and
  non-Windows platform tokens in path components (`unix`, `linux`,
  `posix`, `darwin`, `android`, ... — token-exact so `unistd` /
  `pthreads` survive).
- binary tarball dep (cef) → INTERFACE shim with `XER_<NAME>_PAYLOAD_DIR`,
  `_LIB_DIR`, `_LIBRARIES` (`.lib`/`.a` only) and `_RUNTIME_DLLS`.
- autotools-only dep (ffmpeg, libxml2 w/o cmake) → STATIC skeleton +
  `message(WARNING ... requires configure ...)` —
  `XER_<NAME>_REQUIRES_CONFIGURE` is set. clang analysis can enumerate
  sources and the include graph but cannot produce generated headers
  (`config.h`, `libavcodec/config.h`); those deps need their real
  configure step. This is the main documented gap.

## Other library reuse decisions

- **JSON**: `jsoncpp` via `XER_JsonCpp` (repo-wide choice).
- **tarballs**: `XER::Content::Archive::ArchiveManager::Extract`
  (`src/content/archive`, libarchive — bzip2 tar covered).
- **downloads**: `CURL::libcurl` write-callback streaming to disk —
  not `XER_NetClient::DownloadFile`, which buffers the entire body in
  memory (wrong shape for the ~300 MB CEF tarball). `iphlpapi` is
  linked for static libcurl's `if_nametoindex`.
- **git**: invoked through the `git` CLI on PATH (deliberate — no
  libgit2 in the repo; honest clone/fetch semantics). Clones are
  `--depth 1` because ffmpeg alone is multi-GB.

## Known gaps

- `verify` is a *static* check (paths/commands/parens), not a real
  `cmake -P`/`--dep-scoped` configure — dry-running add_library blocks
  would need a real sub-configure per dep.
- Wrapper patches guess upstream target names from `add_library(`
  regexes — a dep declaring its lib through a macro
  (`absl_cc_library`, ffmpeg's non-cmake build) yields an "unmatched"
  patch the consumer edits.
- The source-filter heuristics are tuned to Windows; cross-platform
  patch generation would need a `--platform` flag (tokens are already
  data-driven).
- `auto` deliberately does not mass-clone: `Build.ps1` calling it at
  startup gets the cheap check + interval-gated fetches + patches;
  cold-start population is `xer_sync_dev auto --fetch` or an explicit
  `sync`.

## Manual smoke

```sh
cmake --build --preset windows-clang-msys2 --target xer_sync_dev
./out/build/windows-clang-msys2/bin/xer_sync_dev.exe selftest
./out/build/windows-clang-msys2/bin/xer_sync_dev.exe status third_party/3RD.xersyunc
./out/build/windows-clang-msys2/bin/xer_sync_dev.exe cmake-gen third_party/3RD.xersyunc --dep zlib
./out/build/windows-clang-msys2/bin/xer_sync_dev.exe verify third_party/3RD.xersyunc
./out/build/windows-clang-msys2/bin/xer_sync_dev.exe sync third_party/3RD.xersyunc --check-only
```
