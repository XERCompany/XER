# Shim: range-v3 is header-only (<range/v3/...>). Debian ships a CMake
# config, MSYS2 may or may not — find_path covers every case. On
# Linux/macOS the partial-vcpkg share may provide the real port config;
# defer to it so this shim never shadows an authoritative config.
set(XER_DEFER_PKG "range-v3")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    find_path(XER_RANGE_V3_INCLUDE_DIR NAMES range/v3/all.hpp)
    if(XER_RANGE_V3_INCLUDE_DIR)
        if(NOT TARGET range-v3::range-v3)
            add_library(range-v3::range-v3 INTERFACE IMPORTED GLOBAL)
            set_target_properties(range-v3::range-v3 PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${XER_RANGE_V3_INCLUDE_DIR}"
                INTERFACE_COMPILE_FEATURES "cxx_std_14")
        endif()
        set(range-v3_FOUND TRUE)
    else()
        set(range-v3_FOUND FALSE)
    endif()
else()
    set(range-v3_FOUND TRUE)
endif()
