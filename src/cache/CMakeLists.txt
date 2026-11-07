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

# Link with vcpkg libraries
target_link_libraries(XER_Cache
    PUBLIC
        absl::strings
        absl::synchronization
        unofficial::sqlite3::sqlite3
        ZLIB::ZLIB
)

# C++ standard
target_compile_features(XER_Cache PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_Cache PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Cache PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
