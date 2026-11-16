# Shim: cereal is header-only (<cereal/...>) and ships no usable CMake
# config on Debian/MSYS2 — locate the headers directly. On Linux/macOS
# the partial-vcpkg share may provide the real port config; defer to it
# so this shim never shadows an authoritative config.
set(XER_DEFER_PKG "cereal")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    find_path(XER_CEREAL_INCLUDE_DIR NAMES cereal/cereal.hpp)
    if(XER_CEREAL_INCLUDE_DIR)
        if(NOT TARGET cereal::cereal)
            add_library(cereal::cereal INTERFACE IMPORTED GLOBAL)
            set_target_properties(cereal::cereal PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${XER_CEREAL_INCLUDE_DIR}"
                INTERFACE_COMPILE_FEATURES "cxx_std_11")
        endif()
        set(cereal_FOUND TRUE)
    else()
        set(cereal_FOUND FALSE)
    endif()
else()
    set(cereal_FOUND TRUE)
endif()
