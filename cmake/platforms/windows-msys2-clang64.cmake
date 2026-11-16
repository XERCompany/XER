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
# with -DXER_MSYS2_ROOT=... for other layouts (e.g. C:/msys64).
set(XER_MSYS2_ROOT "C:/tools/msys64" CACHE PATH "MSYS2 installation root")
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
# VERIFY-on-spike: MSYS2 .pc files use posix-style prefixes (prefix=/clang64).
# PKG_CONFIG_SYSROOT_DIR rewrites -I/-L flags to real Windows paths for the
# native (non-msys) pkg-config binary. If the spike shows pkgconf or clang
# already resolves /clang64-style paths, this variable can be dropped.
set(ENV{PKG_CONFIG_LIBDIR} "${XER_MSYS2_CLANG64}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH}  "${XER_MSYS2_CLANG64}/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${XER_MSYS2_ROOT}")
