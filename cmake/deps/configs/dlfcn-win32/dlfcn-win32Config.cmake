# Shim: dlfcn-win32 (POSIX dlopen/dlsym/dlclose/dlerror/dladdr over
# LoadLibrary/GetProcAddress) has no MSYS2 package — the sources are
# vendored under third_party/dlfcn-win32 (see
# docs/wiki/research/toolchain-migration-audit.md). On Linux/macOS the
# partial-vcpkg share may provide the real port config instead; defer to
# it so this shim never shadows an authoritative config.
#
# Target contract mirrors the vcpkg port, which exports
# dlfcn-win32::dl (a dl.dll import lib there; a static lib here).
#
# NOTE: nothing in the tree calls find_package(dlfcn-win32) today — the
# same is true under vcpkg, where <dlfcn.h> resolves through the global
# vcpkg include dir and the dl* symbols are never linked (the only
# unguarded consumers — aura_lc3_decoder.cc, pxer_host.cc — live in
# static libs no binary links yet). system_deps.cmake mirrors the
# header side globally on WIN32; this config exists so a future
# target_link_libraries(... dlfcn-win32::dl) works unchanged.
set(XER_DEFER_PKG "dlfcn-win32")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    if(WIN32)
        get_filename_component(_xer_dl_vendor
            "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/dlfcn-win32" ABSOLUTE)

        # dlfcn-win32 is a tiny single-file C library; when the vendored
        # copy is present we build it directly (same as the vcpkg port).
        set(_xer_dl_src "")
        foreach(_xer_dl_cand IN ITEMS
                "${_xer_dl_vendor}/dlfcn.c"
                "${_xer_dl_vendor}/src/dlfcn.c")
            if(EXISTS "${_xer_dl_cand}")
                set(_xer_dl_src "${_xer_dl_cand}")
                break()
            endif()
        endforeach()

        if(_xer_dl_src)
            if(NOT TARGET xer_dlfcn_win32)
                add_library(xer_dlfcn_win32 STATIC "${_xer_dl_src}")
                get_filename_component(_xer_dl_inc "${_xer_dl_src}" DIRECTORY)
                target_include_directories(xer_dlfcn_win32
                    PUBLIC "${_xer_dl_inc}")
                set_target_properties(xer_dlfcn_win32 PROPERTIES
                    POSITION_INDEPENDENT_CODE ON)
                target_compile_features(xer_dlfcn_win32 PRIVATE c_std_99)
            endif()
            if(NOT TARGET dlfcn-win32::dl)
                add_library(dlfcn-win32::dl ALIAS xer_dlfcn_win32)
            endif()
            set(dlfcn-win32_FOUND TRUE)
        else()
            set(dlfcn-win32_FOUND FALSE)
        endif()
    else()
        # Native dlopen: libc (glibc >= 2.34) / libSystem already provide
        # it; CMAKE_DL_LIBS covers older libdl splits.
        if(NOT TARGET dlfcn-win32::dl)
            add_library(dlfcn-win32::dl INTERFACE IMPORTED GLOBAL)
            set_target_properties(dlfcn-win32::dl PROPERTIES
                INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS}")
        endif()
        set(dlfcn-win32_FOUND TRUE)
    endif()
else()
    set(dlfcn-win32_FOUND TRUE)
endif()

unset(_xer_dl_vendor)
unset(_xer_dl_cand)
unset(_xer_dl_src)
unset(_xer_dl_inc)
