# Shim: unofficial-fxdiv is header-only and has no MSYS2 package — vendor
# the fxdiv.h header under third_party/fxdiv (repo root or include/; see
# docs/wiki/research/toolchain-migration-audit.md). On Linux/macOS the
# partial-vcpkg share may provide the real port config instead; defer to
# it so this shim never shadows an authoritative config.
set(XER_DEFER_PKG "unofficial-fxdiv")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    # Vendored headers take priority, then the normal search path
    # (CMAKE_PREFIX_PATH covers <msys2>/clang64/include and any system dir).
    get_filename_component(_xer_fxdiv_vendor
        "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/fxdiv" ABSOLUTE)
    set(_xer_fxdiv_inc "")
    foreach(_xer_fxdiv_cand IN ITEMS
            "${_xer_fxdiv_vendor}"
            "${_xer_fxdiv_vendor}/include"
            "${_xer_fxdiv_vendor}/include/fxdiv")
        if(EXISTS "${_xer_fxdiv_cand}/fxdiv.h")
            set(_xer_fxdiv_inc "${_xer_fxdiv_cand}")
            break()
        endif()
    endforeach()
    if(NOT _xer_fxdiv_inc)
        find_path(XER_FXDIV_INCLUDE_DIR NAMES fxdiv.h)
        set(_xer_fxdiv_inc "${XER_FXDIV_INCLUDE_DIR}")
    endif()

    if(_xer_fxdiv_inc)
        if(NOT TARGET unofficial::fxdiv::fxdiv)
            add_library(unofficial::fxdiv::fxdiv INTERFACE IMPORTED GLOBAL)
            set_target_properties(unofficial::fxdiv::fxdiv PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${_xer_fxdiv_inc}"
                # Mirror the vcpkg port: portable C path, no inline asm.
                INTERFACE_COMPILE_DEFINITIONS "FXDIV_USE_INLINE_ASSEMBLY=0")
        endif()
        set(unofficial-fxdiv_FOUND TRUE)
    else()
        set(unofficial-fxdiv_FOUND FALSE)
    endif()
else()
    set(unofficial-fxdiv_FOUND TRUE)
endif()

unset(_xer_fxdiv_vendor)
unset(_xer_fxdiv_cand)
unset(_xer_fxdiv_inc)
