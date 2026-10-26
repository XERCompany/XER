# ============================================================
# XERWinAPI Bridge - Windows API integration
# ============================================================

# Collect helper sources (dict.cc, etc.)
file(GLOB_RECURSE ENGINE_HELPER_WINAPI_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/helper/dict.cc")

file(GLOB_RECURSE ENGINE_HELPER_WINAPI_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/helper/dict.h")

# Collect platform common sources
file(GLOB_RECURSE ENGINE_PLATFORM_COMMON_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/platform_common.cc")

file(GLOB_RECURSE ENGINE_PLATFORM_COMMON_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/platform_common.h")

# Collect platform-specific sources
set(ENGINE_PLATFORM_CC)
set(ENGINE_PLATFORM_H)

if(WIN32)
    file(GLOB_RECURSE ENGINE_PLATFORM_WIN_CC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/*.cc")
    
    file(GLOB_RECURSE ENGINE_PLATFORM_WIN_H CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/*.h")
    
    # Temporarily exclude network module due to header conflicts
    file(GLOB_RECURSE ENGINE_WINAPI_CC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/winapi/controls/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/winapi/graphics/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/winapi/audio/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/winapi/storage/*.cc"
    )
    
    file(GLOB_RECURSE ENGINE_WINAPI_H CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/winapi/controls/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/winapi/graphics/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/winapi/audio/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/windows/winapi/storage/*.h"
    )
    
    list(APPEND ENGINE_PLATFORM_CC ${ENGINE_PLATFORM_WIN_CC})
    list(APPEND ENGINE_PLATFORM_H ${ENGINE_PLATFORM_WIN_H})
elseif(UNIX AND NOT APPLE)
    file(GLOB_RECURSE ENGINE_PLATFORM_LINUX_CC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/linux/*.cc")
    
    file(GLOB_RECURSE ENGINE_PLATFORM_LINUX_H CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/linux/*.h")
    
    list(APPEND ENGINE_PLATFORM_CC ${ENGINE_PLATFORM_LINUX_CC})
    list(APPEND ENGINE_PLATFORM_H ${ENGINE_PLATFORM_LINUX_H})
elseif(APPLE)
    file(GLOB_RECURSE ENGINE_PLATFORM_APPLE_CC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/apple/*.cc")
    
    file(GLOB_RECURSE ENGINE_PLATFORM_APPLE_H CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/content/platform/apple/*.h")
    
    list(APPEND ENGINE_PLATFORM_CC ${ENGINE_PLATFORM_APPLE_CC})
    list(APPEND ENGINE_PLATFORM_H ${ENGINE_PLATFORM_APPLE_H})
endif()

# Combine all WinAPI bridge sources
set(ENGINE_WINAPI_BRIDGE_CC 
    ${ENGINE_HELPER_WINAPI_CC}
    ${ENGINE_PLATFORM_COMMON_CC}
    ${ENGINE_PLATFORM_CC}
    ${ENGINE_WINAPI_CC})

set(ENGINE_WINAPI_BRIDGE_H
    ${ENGINE_HELPER_WINAPI_H}
    ${ENGINE_PLATFORM_COMMON_H}
    ${ENGINE_PLATFORM_H}
    ${ENGINE_WINAPI_H})

# Create XERWinAPI bridge library
if(ENGINE_WINAPI_BRIDGE_CC)
    add_library(XERWinAPI STATIC ${ENGINE_WINAPI_BRIDGE_CC})
    target_sources(XERWinAPI PRIVATE ${ENGINE_WINAPI_BRIDGE_H})
    
    target_include_directories(XERWinAPI PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${xer_jsoncpp_SOURCE_DIR}/include)
    
    target_compile_definitions(XERWinAPI PRIVATE
        ENGINE_HAS_WINAPI_BRIDGE=1)
    
    # Link with required dependencies
    target_link_libraries(XERWinAPI
        PRIVATE
        XER_AsyncIO
        XER_Cache
        jsoncpp_lib
        absl::strings
    )
    
    # Windows-specific libraries
    if(WIN32)
        target_link_libraries(XERWinAPI PRIVATE
            user32
            gdi32
            shell32
            comctl32
            winmm
            ws2_32
            iphlpapi
        )
    endif()
    
    # Linux-specific libraries
    if(UNIX AND NOT APPLE)
        target_link_libraries(XERWinAPI PRIVATE
            pthread
        )
    endif()
    
    # macOS-specific libraries
    if(APPLE)
        target_link_libraries(XERWinAPI PRIVATE
            "-framework CoreServices"
            "-framework IOKit"
        )
    endif()
endif()
