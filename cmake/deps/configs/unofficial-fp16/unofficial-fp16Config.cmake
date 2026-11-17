# Shim: unofficial-fp16 is header-only (fp16.h) and has no MSYS2 package —
# vendor the headers under third_party/fp16 (repo root or include/; see
# docs/wiki/research/toolchain-migration-audit.md). On Linux/macOS the
# partial-vcpkg share may provide the real port config instead; defer to
# it so this shim never shadows an authoritative config.
#
# NOTE: the vcpkg port links unofficial::psimd::psimd as fp16's SIMD
# backend. psimd is likewise unpackaged on MSYS2 — this shim resolves it
# from a real config, bare psimd.h headers, or leaves fp16 on its scalar
# paths. VERIFY-on-spike whether vendored fp16 needs psimd.h at all.
set(XER_DEFER_PKG "unofficial-fp16")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    # psimd backend (optional — fp16.h has scalar fallbacks).
    if(NOT TARGET unofficial::psimd::psimd)
        find_package(unofficial-psimd CONFIG QUIET)
    endif()
    if(NOT TARGET unofficial::psimd::psimd)
        find_path(XER_PSIMD_INCLUDE_DIR NAMES psimd.h)
        if(XER_PSIMD_INCLUDE_DIR)
            add_library(unofficial::psimd::psimd INTERFACE IMPORTED GLOBAL)
            set_target_properties(unofficial::psimd::psimd PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${XER_PSIMD_INCLUDE_DIR}")
        endif()
    endif()

    get_filename_component(_xer_fp16_vendor
        "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/fp16" ABSOLUTE)
    set(_xer_fp16_inc "")
    foreach(_xer_fp16_cand IN ITEMS
            "${_xer_fp16_vendor}"
            "${_xer_fp16_vendor}/include"
            "${_xer_fp16_vendor}/include/fp16")
        if(EXISTS "${_xer_fp16_cand}/fp16.h")
            set(_xer_fp16_inc "${_xer_fp16_cand}")
            break()
        endif()
    endforeach()
    if(NOT _xer_fp16_inc)
        find_path(XER_FP16_INCLUDE_DIR NAMES fp16.h)
        set(_xer_fp16_inc "${XER_FP16_INCLUDE_DIR}")
    endif()

    if(_xer_fp16_inc)
        if(NOT TARGET unofficial::fp16::fp16)
            add_library(unofficial::fp16::fp16 INTERFACE IMPORTED GLOBAL)
            set_target_properties(unofficial::fp16::fp16 PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${_xer_fp16_inc}")
            if(TARGET unofficial::psimd::psimd)
                set_target_properties(unofficial::fp16::fp16 PROPERTIES
                    INTERFACE_LINK_LIBRARIES "unofficial::psimd::psimd")
            endif()
        endif()
        set(unofficial-fp16_FOUND TRUE)
    else()
        set(unofficial-fp16_FOUND FALSE)
    endif()
else()
    set(unofficial-fp16_FOUND TRUE)
endif()

unset(_xer_fp16_vendor)
unset(_xer_fp16_cand)
unset(_xer_fp16_inc)
