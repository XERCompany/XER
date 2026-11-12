# XER Watch Subsystem CMake Configuration
#
# File-watch / live-reload support: change detection, directory scanning,
# snapshots, lock recovery, recompile service and the watch CLI helpers.
# The recompile service spawns a child process — CreateProcess on Windows,
# fork/exec on POSIX.

file(GLOB WATCH_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB WATCH_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_Watch STATIC ${WATCH_SOURCES} ${WATCH_HEADERS})

target_include_directories(XER_Watch
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_Watch
    PUBLIC
        absl::strings
        fmt::fmt
        range-v3::range-v3
        re2::re2
        $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
        XER_Cache
        XER_ErrorHandler
        XER_Flux_Core
        XER_Helper
)

target_compile_features(XER_Watch PUBLIC cxx_std_23)

if(WIN32)
    target_link_libraries(XER_Watch PRIVATE)
endif()

if(MSVC)
    target_compile_options(XER_Watch PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Watch PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
