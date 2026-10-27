# XER Async I/O CMake Configuration

# Collect async_io sources
file(GLOB_RECURSE ASYNC_IO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE ASYNC_IO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create async_io library
add_library(XER_AsyncIO STATIC ${ASYNC_IO_SOURCES} ${ASYNC_IO_HEADERS})

target_include_directories(XER_AsyncIO
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link with vcpkg libraries
target_link_libraries(XER_AsyncIO
    PUBLIC
        absl::synchronization
        absl::flat_hash_map
        absl::flat_hash_set
)

# Link with libuv
if(TARGET libuv::uv_a OR TARGET libuv::uv)
    target_link_libraries(XER_AsyncIO PUBLIC $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
endif()

# C++ standard
target_compile_features(XER_AsyncIO PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_AsyncIO PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_AsyncIO PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(XER_AsyncIO PRIVATE ws2_32)
endif()
