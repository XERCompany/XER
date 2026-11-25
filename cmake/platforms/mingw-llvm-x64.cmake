# Toolchain file: Windows x86_64 via llvm-mingw (mstorsjo release: clang +
# libc++ + lld + UCRT) — the standalone replacement for MSYS2 clang64.
#
#   cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/platforms/mingw-llvm-x64.cmake ...
#
# Same Itanium ABI / libc++ / UCRT / lld toolchain family as MSYS2 clang64,
# so all if(MINGW) machinery in the tree keeps working. The toolchain is
# fetched by xer_sync_dev into third_party/llvm-mingw (dep "llvm-mingw" in
# third_party/3RD.xersyunc) and needs no MSYS2 on PATH — it even ships its
# own busybox (sh/sed/awk/make/...) under busybox/bin and python under
# python/bin for build-time scripting.
#
# CMAKE_SYSTEM_NAME stays "Windows" (native build, not a cross-compile), so
# all if(WIN32) paths in the tree remain active. CMAKE_MSVC_RUNTIME_LIBRARY
# must NOT be set: the runtime is libc++/mingw-w64 CRT on UCRT, not the
# MSVC CRT.

set(CMAKE_SYSTEM_NAME Windows)

# Toolchain root: in-repo third_party/ by default; override with
# -DXER_LLVM_MINGW_ROOT=... to point at an external llvm-mingw install.
get_filename_component(_xer_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(XER_LLVM_MINGW_ROOT "${_xer_repo_root}/third_party/llvm-mingw"
    CACHE PATH "llvm-mingw toolchain root (contains bin/clang.exe)")
if(NOT EXISTS "${XER_LLVM_MINGW_ROOT}/bin/clang++.exe")
    message(FATAL_ERROR
        "llvm-mingw toolchain not found at ${XER_LLVM_MINGW_ROOT} — "
        "fetch it with: xer_sync_dev sync third_party/3RD.xersyunc --dep llvm-mingw "
        "or pass -DXER_LLVM_MINGW_ROOT=<path>")
endif()
set(XER_LLVM_MINGW_BIN     "${XER_LLVM_MINGW_ROOT}/bin")
# The x86_64 target prefix: CRT objects, import libs and runtime DLLs.
set(XER_LLVM_MINGW_SYSROOT "${XER_LLVM_MINGW_ROOT}/x86_64-w64-mingw32")

# Toolchain bin first on PATH: clang finds lld and its runtime DLLs
# (libc++.dll, libunwind.dll, libwinpthread-1.dll) there, and build-time
# tools resolve inside the toolchain instead of MSYS2.
set(ENV{PATH} "${XER_LLVM_MINGW_BIN};$ENV{PATH}")

# Target triple — the toolchain's default (bin/x86_64-w64-windows-gnu.cfg
# already injects -stdlib=libc++ -rtlib=compiler-rt -unwindlib=libunwind
# -fuse-ld=lld); set explicitly for determinism.
set(CMAKE_C_COMPILER_TARGET   x86_64-w64-mingw32)
set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   "${XER_LLVM_MINGW_BIN}/clang.exe"   CACHE FILEPATH "C compiler"   FORCE)
set(CMAKE_CXX_COMPILER "${XER_LLVM_MINGW_BIN}/clang++.exe" CACHE FILEPATH "C++ compiler" FORCE)

# LLVM binutils (unprefixed names target x86_64 via the .cfg files; the
# x86_64-w64-mingw32-* prefixed variants are equivalent).
set(CMAKE_AR      "${XER_LLVM_MINGW_BIN}/llvm-ar.exe"      CACHE FILEPATH "ar"      FORCE)
set(CMAKE_RANLIB  "${XER_LLVM_MINGW_BIN}/llvm-ranlib.exe"  CACHE FILEPATH "ranlib"  FORCE)
set(CMAKE_NM      "${XER_LLVM_MINGW_BIN}/llvm-nm.exe"      CACHE FILEPATH "nm"      FORCE)
set(CMAKE_LINKER  "${XER_LLVM_MINGW_BIN}/ld.lld.exe"       CACHE FILEPATH "linker"  FORCE)
set(CMAKE_OBJCOPY "${XER_LLVM_MINGW_BIN}/llvm-objcopy.exe" CACHE FILEPATH "objcopy" FORCE)
set(CMAKE_OBJDUMP "${XER_LLVM_MINGW_BIN}/llvm-objdump.exe" CACHE FILEPATH "objdump" FORCE)
set(CMAKE_STRIP   "${XER_LLVM_MINGW_BIN}/llvm-strip.exe"   CACHE FILEPATH "strip"   FORCE)
set(CMAKE_DLLTOOL "${XER_LLVM_MINGW_BIN}/llvm-dlltool.exe" CACHE FILEPATH "dlltool" FORCE)

# Resource compiler: bin/windres.exe is llvm-windres (llvm-rc in GNU-windres
# mode); CMake's clang-gnu RC rules drive it with `-O coff`. llvm-rc.exe is
# also shipped if an rc.exe-style frontend is ever needed.
set(CMAKE_RC_COMPILER "${XER_LLVM_MINGW_BIN}/windres.exe" CACHE FILEPATH "RC compiler" FORCE)

# Find-root discipline: llvm-mingw is NOT a classic sysroot (headers at
# <root>/include, libs at <root>/<triple>/lib) so CMAKE_SYSROOT stays unset —
# clang resolves headers/libs relative to its own binary. Instead,
# CMAKE_FIND_ROOT_PATH re-roots every find_*/find_package lookup so nothing
# under MSYS2 (C:/tools/msys64), vcpkg or other host prefixes can satisfy a
# search. The repo root is a root too: absolute paths inside a root are
# searched verbatim (not re-rooted), which keeps in-tree dep locations —
# third_party/* and the shim configs system_deps.cmake appends to
# CMAKE_PREFIX_PATH — working while everything outside stays invisible.
list(APPEND CMAKE_FIND_ROOT_PATH
     "${XER_LLVM_MINGW_ROOT}"
     "${XER_LLVM_MINGW_SYSROOT}"
     "${_xer_repo_root}")
# Host tools (ninja, cmake, python) still resolve normally via PATH — only
# libraries/headers/packages are confined to the roots.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Prefixes inside the roots — searched verbatim: the toolchain root covers
# find_path of the mingw headers (<root>/include) and find_library of the
# clang runtime bits (<root>/lib); the mingw target prefix covers
# find_library of the CRT/import libs (<triple>/lib); third_party covers
# vendored deps that install to a conventional <pkg>/{include,lib} layout.
list(APPEND CMAKE_PREFIX_PATH
     "${XER_LLVM_MINGW_ROOT}"
     "${XER_LLVM_MINGW_SYSROOT}"
     "${_xer_repo_root}/third_party")

# pkg-config: llvm-mingw ships none (the manifest carries a pkg-config-lite
# dep for .pc strategy A). No .pc directory exists inside the toolchain, so
# no PKG_CONFIG_* env is wired here — whichever dep file owns pkg-config
# must set PKG_CONFIG_LIBDIR to the generated .pc sysroot to keep MSYS2's
# .pc files unreachable.
