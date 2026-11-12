# XER Runtime Safety Subsystem CMake Configuration
#
# Startup integrity checks, sandboxing, memory/thread guards used by the
# xer runtime entry path (RuntimeSafety::BootstrapStartup). Portable across
# Windows/Linux/macOS; engine_params.cc is compiled in because it is the
# shared env-parsing helper for startup options.

file(GLOB RUNTIME_SAFETY_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB RUNTIME_SAFETY_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_RuntimeSafety STATIC
    ${RUNTIME_SAFETY_SOURCES}
    ${RUNTIME_SAFETY_HEADERS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine_params.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine_params.h
)

target_include_directories(XER_RuntimeSafety
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_RuntimeSafety
    PUBLIC
        absl::strings
        absl::synchronization
        fmt::fmt
        range-v3::range-v3
        re2::re2
        XER_Helper
        XER_AsyncIO
        XER_Cache
        XER_Flux_Core
        XER_ErrorHandler
)

target_compile_features(XER_RuntimeSafety PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_RuntimeSafety PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_RuntimeSafety PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
