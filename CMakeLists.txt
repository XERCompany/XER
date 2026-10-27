cmake_minimum_required(VERSION 3.21)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

# Explicit vcpkg path (C:/vcpkg)
if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

# Set vcpkg triplet to x64-windows-release
set(VCPKG_TARGET_TRIPLET "x64-windows-release" CACHE STRING "Vcpkg target triplet")
set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "Use preinstalled vcpkg packages")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "MSVC runtime")

project(XER LANGUAGES C CXX)
include(GNUInstallDirs)
include(CTest)

if(VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-release")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR "x64-windows-release requires Release or RelWithDebInfo, not Debug.")
    endif()
    if(CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "Release;RelWithDebInfo" CACHE STRING "Build configurations" FORCE)
    endif()
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "Release;")
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "Release;")
endif()

# Build options
option(XER_FETCH_DEPENDENCIES "Fetch dependencies automatically" OFF)
option(XER_FETCH_V8 "Fetch V8 from source" OFF)
option(ENABLE_V8 "Build V8 and JavaScript runtime" OFF)
option(ENABLE_MEDIA "Enable media dependencies" ON)
option(ENABLE_CUDA "Enable CUDA support" OFF)
option(ENABLE_CUDNN "Enable cuDNN support" OFF)
option(ENABLE_OPENCV "Fetch and build OpenCV" OFF)
option(ENABLE_AI "Enable AI and model-builder sources" OFF)
option(XER_BUILD_APP "Build the XER application and its subsystems" ON)

# Dependency cache directory (only used if XER_FETCH_DEPENDENCIES is ON)
set(XER_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH "Directory for dependency cache")

# V8 dependency cache directory (only used if XER_FETCH_V8 is ON)
set(XER_V8_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/v8" CACHE PATH "Directory for V8 dependency cache")

# Add subdirectories

# Find vcpkg packages
find_package(libuv CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(unofficial-pthreadpool CONFIG REQUIRED)
find_package(unofficial-fxdiv CONFIG REQUIRED)
if(XER_BUILD_APP)
    find_package(FFTW3f CONFIG REQUIRED)
endif()

# Find FFmpeg via pkg-config or direct find
if(ENABLE_MEDIA)
    if(POLICY CMP0174)
        cmake_policy(SET CMP0174 NEW)
    endif()
    find_package(FFMPEG REQUIRED)
    add_library(XER_FFmpeg INTERFACE)
    target_include_directories(XER_FFmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(XER_FFmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(XER_FFmpeg INTERFACE ${FFMPEG_LIBRARIES})
endif()

# Find OpenCV if enabled
if(ENABLE_OPENCV)
    find_package(OpenCV CONFIG REQUIRED)
endif()
if(ENABLE_CUDA OR ENABLE_CUDNN)
    find_package(CUDAToolkit REQUIRED)
endif()
if(ENABLE_CUDNN)
    find_package(CUDNN REQUIRED)
endif()

# Find SDL2 for audio
if(ENABLE_MEDIA)
    find_package(SDL2 CONFIG REQUIRED)
endif()

if(ENABLE_V8)
    include(cmake/v8.cmake)
endif()

if(XER_BUILD_APP)
    add_subdirectory(src)
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Link common vcpkg libraries to xer target
if(TARGET xer)
    target_link_libraries(xer PRIVATE
        $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
        absl::strings
        absl::synchronization
        absl::flat_hash_map
        absl::flat_hash_set
        fmt::fmt
        jsoncpp_lib
        re2::re2
        unofficial::sqlite3::sqlite3
        ZLIB::ZLIB
        zstd::libzstd
        unofficial::brotli::brotlienc
        unofficial::brotli::brotlidec
        unofficial::brotli::brotlicommon
    )

    # Link OpenSSL if available
    if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
        target_link_libraries(xer PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()

    # Link libcurl if available
    if(TARGET CURL::libcurl)
        target_link_libraries(xer PRIVATE CURL::libcurl)
    endif()

    # Link FFmpeg if available
    if(TARGET XER_FFmpeg)
        target_link_libraries(xer PRIVATE XER_FFmpeg)
    endif()

    # Link SDL2 if available
    if(TARGET SDL2::SDL2)
        target_link_libraries(xer PRIVATE SDL2::SDL2)
    endif()

    # Link OpenCV if enabled
    if(ENABLE_OPENCV)
        target_link_libraries(xer PRIVATE
            opencv_core
            opencv_imgproc
            opencv_imgcodecs
        )
    endif()
    if(ENABLE_V8)
        target_link_libraries(xer PRIVATE V8::V8)
    endif()
endif()