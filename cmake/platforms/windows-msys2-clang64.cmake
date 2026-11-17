# Toolchain file: Windows x86_64 via MSYS2 clang64 (clang + libc++, Itanium ABI).
#
#   cmake --preset windows-clang-msys2
#   # or: cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/platforms/windows-msys2-clang64.cmake ...
#
# Dependencies resolve via cmake/deps/system_deps.cmake: MSYS2 .pc/pkg-config
# files and the shim configs under cmake/deps/configs/, plus a few vendored
# headers under third_party/. No vcpkg toolchain is used — clang64 packages
# are Itanium-ABI/libc++ and incompatible with MSVC-ABI vcpkg ports.
#
# CMAKE_SYSTEM_NAME stays "Windows" (native build, not a cross-compile), so
# all if(WIN32) paths in the tree remain active. CMAKE_MSVC_RUNTIME_LIBRARY
# must NOT be set: the clang64 runtime is libc++/mingw CRT, not the MSVC CRT.

set(CMAKE_SYSTEM_NAME Windows)

# MSYS2 root. The choco msys2 package installs to C:\tools\msys64; override
# with -DXER_MSYS2_ROOT=... for other layouts (CI's setup-msys2 lands at
# D:/a/_temp/msys64). When the configured root does not actually contain
# clang64, fall back to wherever clang.exe sits on PATH — a wrong cached
# root is worse than a resolved one.
set(XER_MSYS2_ROOT "C:/tools/msys64" CACHE PATH "MSYS2 installation root")
if(NOT EXISTS "${XER_MSYS2_ROOT}/clang64/bin/clang.exe")
    find_program(_xer_clang_probe NAMES clang clang.exe
                 PATHS ENV PATH NO_DEFAULT_PATH)
    if(_xer_clang_probe)
        get_filename_component(_xer_clang_bin "${_xer_clang_probe}" DIRECTORY)
        get_filename_component(_xer_clang_env "${_xer_clang_bin}/.." REALPATH)
        get_filename_component(_xer_root_guess "${_xer_clang_env}/.." REALPATH)
        if(EXISTS "${_xer_root_guess}/clang64/bin/clang.exe")
            set(XER_MSYS2_ROOT "${_xer_root_guess}" CACHE PATH
                "MSYS2 installation root" FORCE)
            message(STATUS "XER_MSYS2_ROOT resolved to ${XER_MSYS2_ROOT}")
        endif()
    endif()
endif()
set(XER_MSYS2_CLANG64 "${XER_MSYS2_ROOT}/clang64")

# clang64/bin holds clang/clang++, pkg-config, ar/ranlib and the DLL import
# libs' siblings — put it first on PATH so CMake's compiler/pkg-config
# detection and build-time tools all resolve there.
set(ENV{PATH} "${XER_MSYS2_CLANG64}/bin;$ENV{PATH}")

set(CMAKE_C_COMPILER   clang   CACHE FILEPATH "C compiler"   FORCE)
set(CMAKE_CXX_COMPILER clang++ CACHE FILEPATH "C++ compiler" FORCE)

# Search roots: headers (<root>/include), libraries (<root>/lib — .dll.a
# import libs and .a) and CMake package configs (<root>/lib/cmake/<pkg>)
# all live under the clang64 prefix, so a single CMAKE_PREFIX_PATH entry
# covers find_path/find_library/find_package.
list(APPEND CMAKE_PREFIX_PATH "${XER_MSYS2_CLANG64}")

# pkg-config: force the clang64 .pc directory so msys-repo .pc files can
# never leak into a clang64 build. PKG_CONFIG_LIBDIR replaces pkgconf's
# compiled-in search path; PKG_CONFIG_PATH is appended on top (same dir,
# kept for tools that only honor PATH).
#
# PKG_CONFIG_SYSROOT_DIR must NOT be set: MSYS2's own pkgconf already
# expands posix-style .pc prefixes (prefix=/clang64) against its
# compiled-in install root. A mismatched sysroot prepends itself to
# those already-absolute paths and produces concatenated garbage like
# "C:/tools/msys64D:/a/_temp/msys64/clang64/include" — the exact CI
# failure this fixes. Explicitly unset to also kill an inherited env.
set(ENV{PKG_CONFIG_LIBDIR} "${XER_MSYS2_CLANG64}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH}  "${XER_MSYS2_CLANG64}/lib/pkgconfig")
unset(ENV{PKG_CONFIG_SYSROOT_DIR})
