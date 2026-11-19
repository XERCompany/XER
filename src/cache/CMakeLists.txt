# XER Cache CMake Configuration

# Collect cache sources
file(GLOB_RECURSE CACHE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND CACHE_SOURCES ${_xer_apple_mm})
endif()

file(GLOB_RECURSE CACHE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(_cache_public_deps
    absl::strings
    absl::synchronization
    unofficial::sqlite3::sqlite3
    ZLIB::ZLIB
    OpenSSL::Crypto)

if(XER_YA_DLLS)
    xer_ya_module(YA_CacheShared EXPORT_NAME YA.CacheShared
        SOURCES shared_memory_channel.cc shared_memory_channel.h
        DEPS ${_cache_public_deps} YA_HelperBase)
    list(REMOVE_ITEM CACHE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/shared_memory_channel.cc")
    # helper/dict.cc calls Cache::CacheManager — lives here so
    # HelperBase stays acyclic (CacheShared/Core -> HelperBase).
    xer_ya_module(YA_CacheCore EXPORT_NAME YA.CacheCore
        SOURCES ${CACHE_SOURCES} ${CACHE_HEADERS}
            ${CMAKE_SOURCE_DIR}/src/helper/dict.cc
        DEPS ${_cache_public_deps} YA_CacheShared YA_HelperBase
             XER_JsonCpp)
    xer_ya_bundle(XER_Cache
        MODULES YA_CacheShared YA_CacheCore
        DEPS ${_cache_public_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
else()
    # Create cache library
    add_library(XER_Cache STATIC
        ${CACHE_SOURCES}
        ${CACHE_HEADERS}
    )
    target_include_directories(XER_Cache
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_link_libraries(XER_Cache
        PUBLIC ${_cache_public_deps})
endif()

if(NOT XER_YA_DLLS)
    target_compile_features(XER_Cache PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(XER_Cache PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_Cache PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
endif()
