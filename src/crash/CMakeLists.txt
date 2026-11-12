# XER Crash Subsystem CMake Configuration
#
# V8-backed crash handler: POSIX/Windows signal handlers that write
# structured dump reports. Only built by configurations that provide the
# engine (ENABLE_V8 + V8::V8 target); wired from src/error_handler.

file(GLOB CRASH_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB CRASH_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_Crash STATIC ${CRASH_SOURCES} ${CRASH_HEADERS})

target_include_directories(XER_Crash
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_Crash
    PUBLIC
        absl::strings
        fmt::fmt
        V8::V8
)

target_compile_features(XER_Crash PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_Crash PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Crash PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

if(WIN32)
    # Stack walking for crash reports.
    target_link_libraries(XER_Crash PRIVATE dbghelp)
endif()
