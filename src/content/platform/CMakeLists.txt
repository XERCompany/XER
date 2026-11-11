# XER Content Platform Subsystem CMake Configuration
#
# OS-abstraction layer (Chromium-style content/platform): per-OS
# platform objects (windows/linux/apple) plus platform_common.
# Only the host platform's sources are compiled.

file(GLOB_RECURSE PLATFORM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND PLATFORM_SOURCES ${_xer_apple_mm})
endif()

file(GLOB_RECURSE PLATFORM_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

if(WIN32)
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "/platform/(linux|apple)/")
    list(FILTER PLATFORM_HEADERS EXCLUDE REGEX "/platform/(linux|apple)/")
elseif(APPLE)
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "/platform/(linux|windows)/")
    list(FILTER PLATFORM_HEADERS EXCLUDE REGEX "/platform/(linux|windows)/")
else()
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "/platform/(apple|windows)/")
    list(FILTER PLATFORM_HEADERS EXCLUDE REGEX "/platform/(apple|windows)/")
endif()

add_library(XER_ContentPlatform STATIC ${PLATFORM_SOURCES} ${PLATFORM_HEADERS})

target_include_directories(XER_ContentPlatform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_ContentPlatform
    PUBLIC
        absl::strings
        fmt::fmt
        XER_Helper
)

target_compile_features(XER_ContentPlatform PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentPlatform PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentPlatform PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

if(WIN32)
    target_link_libraries(XER_ContentPlatform PRIVATE
        ws2_32 ole32 oleaut32 uuid shlwapi shell32 user32 gdi32 advapi32)
endif()
