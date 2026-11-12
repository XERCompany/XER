# XER Content Process Subsystem CMake Configuration
#
# Chromium-style content/process layer: process spawn/monitor,
# command-line parsing and exit-status reporting, built on
# XER_AsyncIO's thread pool and platform APIs.

file(GLOB_RECURSE PROCESS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB_RECURSE PROCESS_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_ContentProcess STATIC ${PROCESS_SOURCES} ${PROCESS_HEADERS})

target_include_directories(XER_ContentProcess
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_ContentProcess
    PUBLIC
        absl::strings
        fmt::fmt
        XER_AsyncIO
        XER_Helper
)

target_compile_features(XER_ContentProcess PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentProcess PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentProcess PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

if(WIN32)
    target_link_libraries(XER_ContentProcess PRIVATE
        ws2_32 psapi)
endif()
