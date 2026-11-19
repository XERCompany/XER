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

set(_process_public_deps
    absl::strings
    fmt::fmt
    XER_AsyncIO
    XER_Helper)

if(XER_YA_DLLS)
    xer_ya_module(YA_IPCProcess EXPORT_NAME YA.IPCProcess
        SOURCES process.cc process.h
        DEPS ${_process_public_deps} YA_IPCHandle)
    if(WIN32)
        target_link_libraries(YA_IPCProcess PRIVATE ws2_32 psapi)
    endif()
    xer_ya_module(YA_IPCHandle EXPORT_NAME YA.IPCHandle
        SOURCES process_command_parser.cc process_command_parser.h
        DEPS ${_process_public_deps})
    xer_ya_bundle(XER_ContentProcess
        MODULES YA_IPCProcess YA_IPCHandle
        DEPS ${_process_public_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/../..)
else()
    add_library(XER_ContentProcess STATIC
        ${PROCESS_SOURCES} ${PROCESS_HEADERS})
    target_include_directories(XER_ContentProcess
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
    )
    target_link_libraries(XER_ContentProcess
        PUBLIC ${_process_public_deps})
endif()

if(NOT XER_YA_DLLS)
    target_compile_features(XER_ContentProcess PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(XER_ContentProcess PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_ContentProcess PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
endif()

if(WIN32 AND NOT XER_YA_DLLS)
    target_link_libraries(XER_ContentProcess PRIVATE
        ws2_32 psapi)
endif()
