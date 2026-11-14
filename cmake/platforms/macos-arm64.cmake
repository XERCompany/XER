# Toolchain file: macOS (Apple Silicon / arm64).
#
#   cmake --preset macos-arm64
#
# Homebrew lives in /opt/homebrew on Apple Silicon; add it to the package
# search path so find_package/pkg-config discover brew-installed deps.
# Heavy ports without brew equivalents still come from a partial vcpkg
# install (see cmake/deps/system_deps.cmake, triplet arm64-osx).

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target arch" FORCE)
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS" FORCE)
endif()

foreach(_brew_prefix "/opt/homebrew" "/usr/local")
    if(IS_DIRECTORY "${_brew_prefix}")
        list(APPEND CMAKE_PREFIX_PATH "${_brew_prefix}")
        set(ENV{PKG_CONFIG_PATH} "${_brew_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
endforeach()
