# XER Content Device Subsystem CMake Configuration
#
# Chromium-style content/device layer: Bluetooth (+GATT), USB
# enumeration, Wi-Fi scan/connect, VR session/input and Cast
# (Aura/Mira) sessions, behind the DeviceManager facade.
# POSIX-only paths are ifdef-guarded in the sources; on Windows the
# stubs report "unsupported" until a WinRT/backend lands.

file(GLOB_RECURSE DEVICE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND DEVICE_SOURCES ${_xer_apple_mm})
endif()

file(GLOB_RECURSE DEVICE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_ContentDevice STATIC ${DEVICE_SOURCES} ${DEVICE_HEADERS})

target_include_directories(XER_ContentDevice
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_ContentDevice
    PUBLIC
        absl::strings
        absl::synchronization
        fmt::fmt
        range-v3::range-v3
        XER_AsyncIO
        XER_AudioCore
        XER_Network
        XER_Helper
)

target_compile_features(XER_ContentDevice PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentDevice PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentDevice PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

if(WIN32)
    target_link_libraries(XER_ContentDevice PRIVATE ws2_32 crypt32 bcrypt)
endif()
