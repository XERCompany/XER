# Decision: replace vcpkg with choco + msys2 + clang/ninja

**Status:** APPROVED direction (2026-11-15) — user ruled "полностью
сносить vcpkg": vcpkg presets get REMOVED, not kept alongside. CUDA/
cuDNN/V8 have no mingw path → accepted feature loss (bridges
self-disable; v8.cmake hard-requirement becomes moot). New preset
`windows-clang-msys2` (MSYS2 clang64) becomes THE Windows toolchain.
Sequencing: migration repo-work proceeds IN PARALLEL with Phase 1C
(video_editor files are disjoint); InstallTools.ps1 written from the
audit-verified package list; live spike (skia D3D / MKL link) deferred
until user runs the installer (needs admin).

## What the plan asks

- Single-file `InstallTools.ps1`: choco bootstrap → choco packages
  (nodejs-lts, python3, visualstudio2026buildtools, msys2, ninja, llvm,
  nasm, cuda) → MKL → ~20 `pacman -S mingw-w64-clang-x86_64-*` packages
  → cmake configure with Ninja + Clang.
- "Полностью перестроить систему сборки" — full build-system rebuild,
  not a side preset.

## Audit result (2026-11-15) — FEASIBLE with corrections

Full report: [research/toolchain-migration-audit.md](../research/toolchain-migration-audit.md)

- MSYS2 `clang64` covers ~90% incl. folly/ggml/onnxruntime.
- Plan's package list incomplete (~35 more needed) + ABI
  self-contradiction (choco clang-cl vs msys2 clang-mingw — pick
  clang64; CUDA/V8 have no mingw path → keep vcpkg presets).
- Hard blockers: pthreadpool/fxdiv/fp16/dlfcn-win32 (vendored, tiny),
  MKL (winget oneAPI), skia Ganesh-D3D feature gap (verify first).
- Corrected installer package list + repo-side change list in audit.

## Open risks flagged at intake (2026-11-15)

- **ABI split**: choco `llvm` = clang-cl (MSVC ABI, needs VS/WinSDK);
  msys2 `mingw-w64-clang-x86_64-*` = clang-mingw (different ABI/headers).
  Plan mixes both — must pick ONE toolchain; audit will recommend.
- **`visualstudio2026buildtools` choco ID unverified** — likely
  `visualstudio2022buildtools` or similar; local machine has VS 18
  Community.
- **Dep coverage**: folly, onnxruntime, SDL2, pthreadpool, fxdiv,
  fp16, pffft, tinyexr, zstd, brotli, curl, sqlite3, cudnn — msys2
  coverage unknown; folly/onnxruntime likely absent → features would
  be dropped or stay on vcpkg (semi-migration).
- **MKL** not in choco/msys2 — needs intel oneAPI path.
- **Timing**: Phase 1C mid-flight on MSVC/vcpkg (vcenv.bat warm tree);
  toolchain switch invalidates build tree + all agent build commands.
  Sequencing decision pending with user.

## Links

- [build-and-test-infra](../architecture/build-and-test-infra.md)
- [video-pipeline-facts](../architecture/video-pipeline-facts.md)
