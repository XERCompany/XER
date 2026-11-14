# Toolchain file: macOS (Intel / x86_64).
#
#   cmake --preset macos-x64
#
# Same as macos-arm64.cmake but for Intel Macs; Homebrew prefix is
# /usr/local there and the partial vcpkg triplet is x64-osx.

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Target arch" FORCE)
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS" FORCE)
endif()

foreach(_brew_prefix "/usr/local" "/opt/homebrew")
    if(IS_DIRECTORY "${_brew_prefix}")
        list(APPEND CMAKE_PREFIX_PATH "${_brew_prefix}")
        set(ENV{PKG_CONFIG_PATH} "${_brew_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
endforeach()
