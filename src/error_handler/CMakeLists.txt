# XER Error Handler Subsystem CMake Configuration
#
# Crash monitoring, diagnostics, stack traces and minidump generation.
# err_capture.* compiles without V8: the engine bridge parts are guarded by
# XER_ERROR_HANDLER_HAS_V8 and only activate in ENABLE_V8 configurations that
# also provide the V8::V8 target and the src/crash library.

file(GLOB ERROR_HANDLER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB ERROR_HANDLER_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_ErrorHandler STATIC ${ERROR_HANDLER_SOURCES} ${ERROR_HANDLER_HEADERS})

target_include_directories(XER_ErrorHandler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_ErrorHandler
    PUBLIC
        absl::strings
        absl::synchronization
        fmt::fmt
        range-v3::range-v3
        XER_Helper
        XER_Cache
        XER_Flux_Core
)

# V8-backed crash capture: only when the engine target exists (the xer
# runtime build). err_capture.cc degrades to config-only stubs otherwise.
if(ENABLE_V8 AND TARGET V8::V8)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../crash ${CMAKE_BINARY_DIR}/src/crash)
    target_link_libraries(XER_ErrorHandler PUBLIC XER_Crash V8::V8)
    target_compile_definitions(XER_ErrorHandler PUBLIC XER_ERROR_HANDLER_HAS_V8=1)
endif()

target_compile_features(XER_ErrorHandler PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ErrorHandler PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ErrorHandler PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

if(WIN32)
    # Stack walking + minidump support.
    target_link_libraries(XER_ErrorHandler PRIVATE dbghelp psapi)
endif()
