# XER App CMake Configuration

# Collect app sources
file(GLOB APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
list(APPEND APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../app_command.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../audio_config_helper.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../json_config_helper.cc"
)
if(APPLE)
    file(GLOB _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND APP_SOURCES ${_xer_apple_mm})
endif()

file(GLOB APP_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Lua 5.4 via FetchContent (lua.org is unreachable from this network, so the
# official GitHub mirror is used; hash pinned to the v5.4.7 tag archive).
include(FetchContent)
FetchContent_Declare(lua
    URL https://github.com/lua/lua/archive/refs/tags/v5.4.7.tar.gz
    URL_HASH SHA256=5c39111b3fc4c1c9e56671008955a1730f54a15b95e1f1bd0752b868b929d8e3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(lua)
file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
# onelua.c amalgamates every other .c — compiling both duplicates all symbols.
list(FILTER LUA_SOURCES EXCLUDE REGEX "/onelua\\.c$")
add_library(XER_Lua STATIC ${LUA_SOURCES})
target_include_directories(XER_Lua PUBLIC "${lua_SOURCE_DIR}")
if(MSVC)
    target_compile_options(XER_Lua PRIVATE /W3 /O2)
endif()

# Create app library
add_library(XER_App STATIC
    ${APP_SOURCES}
    ${APP_HEADERS}
)

target_include_directories(XER_App
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link with required libraries
target_link_libraries(XER_App
    PUBLIC
        XER_ContentAudio
        XER_AudioCore
        XER_NodeGraph
        XER_Flux_Core
        XER_AsyncIO
        XER_Cache
        XER_UI_Core
        XER_UI_Flux
        XER_Lua
        XER_Network
        XER_S3
)

# Enables the script.process node (Lua buffer scripting).
target_compile_definitions(XER_App PRIVATE XER_HAS_LUA=1)

# C++ standard
target_compile_features(XER_App PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_App PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_App PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific
if(WIN32)
    # dbghelp: MiniDumpWriteDump + StackWalk64/SymFromAddr for crash reports.
    target_link_libraries(XER_App PRIVATE ole32 dbghelp)
endif()
