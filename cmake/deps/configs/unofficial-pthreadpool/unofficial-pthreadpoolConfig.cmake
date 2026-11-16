# Shim: unofficial-pthreadpool has no MSYS2 package — vendor
# Maratyszcza/pthreadpool under third_party/pthreadpool (repo root, or a
# src/ subdir; see docs/wiki/research/toolchain-migration-audit.md). On
# Linux/macOS the partial-vcpkg share may provide the real port config
# instead; defer to it so this shim never shadows an authoritative config.
#
# Target contract mirrors the vcpkg port: unofficial::pthreadpool links
# unofficial::fxdiv::fxdiv (work-stealing division) and exposes
# pthreadpool.h on its include path.
set(XER_DEFER_PKG "unofficial-pthreadpool")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    get_filename_component(_xer_pp_vendor
        "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/pthreadpool" ABSOLUTE)

    # pthreadpool is a tiny single-file C library; when the vendored copy
    # is present we build it directly (same as the vcpkg port does).
    set(_xer_pp_src "")
    foreach(_xer_pp_cand IN ITEMS
            "${_xer_pp_vendor}/pthreadpool.c"
            "${_xer_pp_vendor}/src/pthreadpool.c")
        if(EXISTS "${_xer_pp_cand}")
            set(_xer_pp_src "${_xer_pp_cand}")
            break()
        endif()
    endforeach()

    if(_xer_pp_src)
        if(NOT TARGET unofficial::pthreadpool)
            add_library(unofficial::pthreadpool STATIC "${_xer_pp_src}")
            get_filename_component(_xer_pp_inc "${_xer_pp_src}" DIRECTORY)
            target_include_directories(unofficial::pthreadpool
                PUBLIC "${_xer_pp_inc}")
            set_target_properties(unofficial::pthreadpool PROPERTIES
                POSITION_INDEPENDENT_CODE ON)
            target_compile_features(unofficial::pthreadpool PRIVATE c_std_11)
            find_package(Threads QUIET)
            if(TARGET Threads::Threads)
                target_link_libraries(unofficial::pthreadpool
                    PUBLIC Threads::Threads)
            endif()
        endif()
        set(unofficial-pthreadpool_FOUND TRUE)
    else()
        # No vendored copy — last resort: a system-installed library
        # (pkg-config is not probed: no pthreadpool .pc exists anywhere).
        find_path(XER_PTHREADPOOL_INCLUDE_DIR NAMES pthreadpool.h)
        find_library(XER_PTHREADPOOL_LIBRARY NAMES pthreadpool)
        if(XER_PTHREADPOOL_INCLUDE_DIR AND XER_PTHREADPOOL_LIBRARY)
            if(NOT TARGET unofficial::pthreadpool)
                add_library(unofficial::pthreadpool UNKNOWN IMPORTED GLOBAL)
                set_target_properties(unofficial::pthreadpool PROPERTIES
                    IMPORTED_LOCATION "${XER_PTHREADPOOL_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES
                        "${XER_PTHREADPOOL_INCLUDE_DIR}")
            endif()
            set(unofficial-pthreadpool_FOUND TRUE)
        else()
            set(unofficial-pthreadpool_FOUND FALSE)
        endif()
    endif()

    # fxdiv is a hard dependency of pthreadpool (the vcpkg port links
    # unofficial::fxdiv::fxdiv); resolve it through the same shim chain.
    # IMPORTED targets reject target_link_libraries(PUBLIC) — their link
    # interface must go through INTERFACE_LINK_LIBRARIES instead.
    if(unofficial-pthreadpool_FOUND)
        find_package(unofficial-fxdiv CONFIG QUIET)
        if(TARGET unofficial::fxdiv::fxdiv AND TARGET unofficial::pthreadpool)
            get_target_property(_xer_pp_is_imported
                unofficial::pthreadpool IMPORTED)
            if(_xer_pp_is_imported)
                set_target_properties(unofficial::pthreadpool PROPERTIES
                    INTERFACE_LINK_LIBRARIES "unofficial::fxdiv::fxdiv")
            else()
                target_link_libraries(unofficial::pthreadpool
                    PUBLIC unofficial::fxdiv::fxdiv)
            endif()
        endif()
    endif()
else()
    set(unofficial-pthreadpool_FOUND TRUE)
endif()

unset(_xer_pp_vendor)
unset(_xer_pp_cand)
unset(_xer_pp_src)
unset(_xer_pp_inc)
unset(_xer_pp_is_imported)
