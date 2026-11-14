# Toolchain file: Linux x86_64 (Ubuntu/Debian + GCC).
#
#   cmake --preset linux-release
#   # or: cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/platforms/linux-x64.cmake ...
#
# Dependencies resolve via cmake/deps/system_deps.cmake (apt packages +
# optional partial vcpkg for heavy ports). No vcpkg toolchain is used.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   gcc  CACHE FILEPATH "C compiler"   FORCE)
set(CMAKE_CXX_COMPILER g++  CACHE FILEPATH "C++ compiler" FORCE)

# pkg-config must find distro .pc files.
find_program(XER_PKG_CONFIG_EXECUTABLE NAMES pkg-config)
if(XER_PKG_CONFIG_EXECUTABLE)
    set(ENV{PKG_CONFIG_PATH} "/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig:/usr/local/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()
