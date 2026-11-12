# XER Error Handler Subsystem CMake Configuration
#
# Crash monitoring, diagnostics, stack traces and minidump generation.
# err_capture.* is excluded: it bridges into V8 and is only built by
# configurations that provide the engine (ENABLE_V8 builds).

file(GLOB ERROR_HANDLER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB ERROR_HANDLER_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

list(FILTER ERROR_HANDLER_SOURCES EXCLUDE REGEX "/err_capture\\.cc$")
list(FILTER ERROR_HANDLER_HEADERS EXCLUDE REGEX "/err_capture\\.h$")

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
        XER_Cache
        XER_Flux_Core
)

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
