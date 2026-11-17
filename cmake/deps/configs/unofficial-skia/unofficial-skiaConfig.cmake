# Shim: unofficial-skia. MSYS2 ships skia pkg-config-only (skia.pc); the
# vcpkg port is a heavy multi-module build. Probe order:
#   1. defer to the partial-vcpkg share config (Linux/macOS heavy ports);
#   2. pkg-config skia.pc (MSYS2 clang64);
#   3. find_library + header probes (generic fallback).
#
# VERIFY-on-spike (HIGH RISK per audit): the msys2 skia package may lack
# the Ganesh D3D12 headers (gpu/ganesh/d3d/*) that src/ui/canvas and
# src/wrapper/skia need, and its .pc may not expose every include layout
# the code probes (<core/...>, "include/core/...", <SkCanvas.h>).
set(XER_DEFER_PKG "unofficial-skia")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    set(_xer_skia_inc "")
    set(_xer_skia_libdirs "")
    set(_xer_skia_libs "")

    # 1) pkg-config .pc (MSYS2). Plain find_package, NOT find_dependency —
    #    find_dependency() returns() from this file when pkg-config is
    #    absent, which would make the header/lib probes below unreachable.
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(XER_SKIA_0 QUIET skia)
    endif()
    if(XER_SKIA_0_FOUND)
        list(APPEND _xer_skia_inc ${XER_SKIA_0_INCLUDE_DIRS})
        list(APPEND _xer_skia_libdirs ${XER_SKIA_0_LIBRARY_DIRS})
        list(APPEND _xer_skia_libs ${XER_SKIA_0_LINK_LIBRARIES})
    endif()

    # 2) Header-layout probes — the code tries several include spellings,
    #    so collect every root that resolves them. MSYS2 nests the public
    #    headers one level deeper: include/skia/include/core/... — probe
    #    the pkg-config include dir with an "include" suffix for that.
    find_path(XER_SKIA_INC_CORE NAMES core/SkCanvas.h
        PATHS ${XER_SKIA_0_INCLUDE_DIRS} PATH_SUFFIXES include)
    find_path(XER_SKIA_INC_FULL NAMES include/core/SkCanvas.h
        PATHS ${XER_SKIA_0_INCLUDE_DIRS})
    find_path(XER_SKIA_INC_FLAT NAMES SkCanvas.h
        PATHS ${XER_SKIA_0_INCLUDE_DIRS} PATH_SUFFIXES include/core)
    list(APPEND _xer_skia_inc
        "${XER_SKIA_INC_CORE}" "${XER_SKIA_INC_FULL}" "${XER_SKIA_INC_FLAT}")
    list(REMOVE_DUPLICATES _xer_skia_inc)
    list(FILTER _xer_skia_inc EXCLUDE REGEX "NOTFOUND$")

    # 3) Library fallback when the .pc gave no link line.
    if(NOT _xer_skia_libs)
        find_library(XER_SKIA_LIBRARY NAMES skia)
        if(XER_SKIA_LIBRARY)
            list(APPEND _xer_skia_libs "${XER_SKIA_LIBRARY}")
        endif()
    endif()

    # Skia is a compiled library — require both headers and a link line,
    # otherwise report not-found so REQUIRED consumers fail at configure
    # time with a clear message instead of at link time.
    if(_xer_skia_inc AND _xer_skia_libs)
        if(NOT TARGET unofficial::skia::skia)
            add_library(unofficial::skia::skia INTERFACE IMPORTED GLOBAL)
            set_target_properties(unofficial::skia::skia PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${_xer_skia_inc}"
                INTERFACE_LINK_DIRECTORIES "${_xer_skia_libdirs}"
                INTERFACE_LINK_LIBRARIES "${_xer_skia_libs}")
        endif()
        set(unofficial-skia_FOUND TRUE)
    else()
        set(unofficial-skia_FOUND FALSE)
    endif()
else()
    set(unofficial-skia_FOUND TRUE)
endif()

unset(_xer_skia_inc)
unset(_xer_skia_libdirs)
unset(_xer_skia_libs)
