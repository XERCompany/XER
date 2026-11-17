# Toolchain migration (vcpkg → choco+msys2+clang64) — SDD-style ledger

Decision (2026-11-15, user): "полностью сносить vcpkg" — vcpkg presets
REMOVED eventually; CUDA/cuDNN/V8 accepted loss; new preset
windows-clang-msys2 = THE Windows toolchain. Work runs in PARALLEL
with Phase 1C (disjoint files).

Key refs:
- docs/wiki/research/toolchain-migration-audit.md (package tables,
  corrected installer list, repo-change spec, risk ranking)
- docs/wiki/decisions/toolchain-migration.md
- ForceChangeManagerPackages.md (user's original plan)

## Tasks
- [ ] InstallTools.ps1 — agent 6e1834bf (single-file installer,
      audit-corrected package list, skips VS/llvm/cuda choco pkgs)
- [ ] CMake groundwork — agent dda1a771 (toolchain file, preset,
      system_deps re-key gated, ~15 shim configs; default preset must
      stay green)
- [ ] Vendor: pthreadpool, fxdiv, fp16, dlfcn-win32 (tiny libs)
- [ ] #pragma comment(lib) audit (~14 files, dwmapi/gdiplus/iphlpapi/
      winhttp/bthprops/setupapi/wlanapi)
- [ ] SPIKE (needs user to run InstallTools.ps1 as admin): skia
      Ganesh-D3D headers check, MKL dynamic-link, folly/libc++ compile,
      OpenCV5 API breaks, _s CRT functions
- [ ] Configure windows-clang-msys2, fix compile/link errors
- [ ] Remove vcpkg path (toolchain file, presets, CI, VCPKG_* refs)
- [ ] Update AGENTS.md dep docs
- [x] InstallTools.ps1 — DONE, committed d14d91b (599 lines, 63 pacman
      pkgs, choco×5, MKL via winget, -WhatIf/params; not executed —
      needs admin = the deferred spike)
- [x] CMake groundwork — DONE, committed a90efac (toolchain file,
      windows-clang-msys2 preset, XER_USE_SYSTEM_DEPS gating —
      default preset verified still-configures, 9 shim configs +
      xer_defer_vcpkg_config helper; default preset green)
- [ ] Vendor: agent 49b89dfc running (pthreadpool/fxdiv/fp16/
      dlfcn-win32 → third_party per shim expectations)
- [ ] Pragma/MSVC-ism audit: agent dcf308d9 running (read-only →
      produces fix-list for mingw link/compile breakage)
- [ ] SPIKE (needs user to run InstallTools.ps1 as admin): skia
      Ganesh-D3D headers check, MKL dynamic-link, folly/libc++ compile,
      OpenCV5 API breaks, _s CRT functions
- [ ] Configure windows-clang-msys2, fix compile/link errors
- [ ] Remove vcpkg path (toolchain file, presets, CI, VCPKG_* refs)
- [ ] Update AGENTS.md dep docs
- [x] Pragma/MSVC-ism audit — DONE (dcf308d9). Report saved
      .superpowers/toolchain-migration/pragma-audit-report.md.
      Parent applied WILL-BREAK-LINK fixes → commit b832807
      (dwmapi+gdiplus→UI_Core, winmm+iphlpapi→ContentPlatform,
      bthprops+setupapi+wlanapi+iphlpapi+ole32→ContentDevice).
      default preset re-verified configures clean (116s).
      Remaining compile-breaks = 2 dlfcn.h files → covered by vendor.
