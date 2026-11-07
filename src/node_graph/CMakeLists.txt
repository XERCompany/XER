
# XER NodeGraph CMake Configuration

file(GLOB_RECURSE NODE_GRAPH_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB_RECURSE NODE_GRAPH_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

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

# Extended node library dependencies: project storage (SQLite via src/sql),
# regex (re2), unicode case mapping (icu uc), compression/checksums (zlib).
# range-v3 is header-only and resolved through the vcpkg include path.
find_package(re2 CONFIG REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc)

target_link_libraries(XER_NodeGraph
    PUBLIC
        absl::strings
        absl::flat_hash_map
        absl::flat_hash_set
        jsoncpp_lib
        XER_SQL
        re2::re2
        ICU::uc
        ZLIB::ZLIB
        MKL::MKL
)

# Compiler flags
if(MSVC)
    target_compile_options(XER_NodeGraph PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_NodeGraph PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific flags
if(WIN32)
    target_compile_definitions(XER_NodeGraph PRIVATE _USE_MATH_DEFINES)
endif()
