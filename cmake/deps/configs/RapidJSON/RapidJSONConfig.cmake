# Shim: RapidJSON is header-only (<rapidjson/...>). NOTE: deliberately does
# NOT set RAPIDJSON_INCLUDE_DIRS — system_deps.cmake has a fallback block
# that would try to add_library(RapidJSON) a second time and fail.
# On Linux/macOS the partial-vcpkg share may provide the real port config;
# defer to it so this shim never shadows an authoritative config.
set(XER_DEFER_PKG "RapidJSON")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    find_path(XER_RAPIDJSON_INCLUDE_DIR NAMES rapidjson/document.h)
    if(XER_RAPIDJSON_INCLUDE_DIR)
        if(NOT TARGET RapidJSON)
            add_library(RapidJSON INTERFACE IMPORTED GLOBAL)
            set_target_properties(RapidJSON PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${XER_RAPIDJSON_INCLUDE_DIR}")
        endif()
        set(RapidJSON_FOUND TRUE)
    else()
        set(RapidJSON_FOUND FALSE)
    endif()
else()
    set(RapidJSON_FOUND TRUE)
endif()
