
# XER NodeGraph CMake Configuration

file(GLOB_RECURSE NODE_GRAPH_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND NODE_GRAPH_SOURCES ${_xer_apple_mm})
endif()
file(GLOB_RECURSE NODE_GRAPH_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(_ng_public_deps
    absl::strings
    absl::flat_hash_map
    absl::flat_hash_set
    XER_JsonCpp
    XER_SQL
    re2::re2
    ICU::uc
    ZLIB::ZLIB)

if(XER_YA_DLLS)
    list(REMOVE_ITEM NODE_GRAPH_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/node_graph_project_store.cc")
    xer_ya_module(YA_ProjectStoreSQL EXPORT_NAME YA.ProjectStoreSQL
        SOURCES node_graph_project_store.cc node_graph_project_store.h
        DEPS ${_ng_public_deps})
    xer_ya_module(YA_NodeGraph EXPORT_NAME YA.NodeGraph
        SOURCES ${NODE_GRAPH_SOURCES} ${NODE_GRAPH_HEADERS}
        DEPS ${_ng_public_deps} YA_ProjectStoreSQL)
    xer_ya_bundle(XER_NodeGraph
        MODULES YA_NodeGraph YA_ProjectStoreSQL
        DEPS ${_ng_public_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
    set(_xer_ng_scope INTERFACE)
else()
    add_library(XER_NodeGraph STATIC
        ${NODE_GRAPH_SOURCES}
        ${NODE_GRAPH_HEADERS}
    )
    target_include_directories(XER_NodeGraph
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_compile_features(XER_NodeGraph PUBLIC cxx_std_23)
    set(_xer_ng_scope PUBLIC)
endif()

# Extended node library dependencies: project storage (SQLite via src/sql),
# regex (re2), unicode case mapping (icu uc), compression/checksums (zlib).
# range-v3 is header-only and resolved through the vcpkg include path.
find_package(re2 CONFIG REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc)



# MKL is optional (absent on macOS): extended DSP nodes fall back to scalar
# loops when it is not found.
find_package(mkl CONFIG QUIET)
if(TARGET MKL::MKL)
    target_link_libraries(XER_NodeGraph ${_xer_ng_scope} MKL::MKL)
    target_compile_definitions(XER_NodeGraph ${_xer_ng_scope} XER_HAVE_MKL)
endif()

# Compiler flags
if(MSVC)
    if(NOT XER_YA_DLLS)
    target_compile_options(XER_NodeGraph PRIVATE /W4 /WX- /O2)
endif()
else()
    if(NOT XER_YA_DLLS)
    target_compile_options(XER_NodeGraph PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
endif()

# Platform-specific flags
if(WIN32)
    if(NOT XER_YA_DLLS)
        target_compile_definitions(XER_NodeGraph PRIVATE _USE_MATH_DEFINES)
    endif()
endif()
