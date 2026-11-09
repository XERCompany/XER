cmake_minimum_required(VERSION 3.21)

project(Editor)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories for binaries and libraries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
endforeach()

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()
 
if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set vcpkg triplet to x64-windows-release
set(VCPKG_TARGET_TRIPLET "x64-windows-release" CACHE STRING "Vcpkg target triplet")
set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "Use preinstalled vcpkg packages")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "MSVC runtime")

set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)
file(GLOB HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/main.h)

find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(ZLIB REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED) 
find_package(zstd CONFIG REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(unofficial-pthreadpool CONFIG REQUIRED)
find_package(unofficial-fxdiv CONFIG REQUIRED) 
find_package(CUDNN REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(range-v3 CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)
find_package(ggml CONFIG REQUIRED)
find_package(folly CONFIG REQUIRED)
find_package(MKL CONFIG REQUIRED)
# Intel MKL available project-wide (e.g. ENGINE_HAS_MKL gates the DFTI
# backend in FFTEngine and other MKL-accelerated DSP paths).
add_compile_definitions(ENGINE_HAS_MKL=1)
find_package(Boost CONFIG REQUIRED)
find_package(cereal CONFIG REQUIRED)
find_package(FFTW3f CONFIG REQUIRED)

# VST3 SDK (vcpkg port "vst3sdk", no cmake-config -> wire manually)
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(XER_VST3SDK_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
else()
    set(XER_VST3SDK_ROOT "C:/vcpkg/installed/x64-windows-release")
endif()
set(XER_VST3SDK_INCLUDE_DIR "${XER_VST3SDK_ROOT}/include/vst3sdk")
set(XER_VST3SDK_LIB_DIR "${XER_VST3SDK_ROOT}/lib/vst3sdk")
foreach(_vst3sdk_file IN ITEMS
    "${XER_VST3SDK_INCLUDE_DIR}/pluginterfaces/base/funknown.h"
    "${XER_VST3SDK_LIB_DIR}/sdk_hosting.lib"
    "${XER_VST3SDK_LIB_DIR}/sdk.lib"
    "${XER_VST3SDK_LIB_DIR}/sdk_common.lib"
    "${XER_VST3SDK_LIB_DIR}/pluginterfaces.lib"
    "${XER_VST3SDK_LIB_DIR}/base.lib")
    if(NOT EXISTS "${_vst3sdk_file}")
        message(FATAL_ERROR "VST3 SDK file missing: ${_vst3sdk_file} — install vcpkg port vst3sdk for ${VCPKG_TARGET_TRIPLET}")
    endif()
endforeach()
add_library(XER_VST3SDK INTERFACE)
target_include_directories(XER_VST3SDK INTERFACE "${XER_VST3SDK_INCLUDE_DIR}")
target_link_libraries(XER_VST3SDK INTERFACE
    "${XER_VST3SDK_LIB_DIR}/sdk_hosting.lib"
    "${XER_VST3SDK_LIB_DIR}/sdk.lib"
    "${XER_VST3SDK_LIB_DIR}/sdk_common.lib"
    "${XER_VST3SDK_LIB_DIR}/pluginterfaces.lib"
    "${XER_VST3SDK_LIB_DIR}/base.lib")

add_subdirectory(src/flux)
add_subdirectory(src/cache)
add_subdirectory(src/wrapper/ffmpeg)
add_subdirectory(src/content/video)
add_subdirectory(src/sql)
add_subdirectory(src/node_graph)
add_subdirectory(src/async_io)
add_subdirectory(src/security_flash)
add_subdirectory(src/content/passwordgen)
add_subdirectory(src/ui)
add_subdirectory(src/audio)
add_subdirectory(src/content/audio)
add_subdirectory(src/content/clipboard)
add_subdirectory(src/content/hex)
add_subdirectory(src/content/network)
add_subdirectory(src/content/nettest)
add_subdirectory(src/content/sim)
add_subdirectory(src/content/model3d)
add_subdirectory(src/content/modeldiff)
add_subdirectory(src/content/formatconv)
add_subdirectory(src/content/dawn)
add_subdirectory(src/content/s3)
add_subdirectory(src/helper)
add_subdirectory(src/app)
add_subdirectory(src/lsui)

# Standalone VST3 scanner — loads plugin DLLs, so it runs as its own
# process away from the editor's UI/audio threads.
add_executable(EngineScanVST3 tools/EngineScanVST3/main.cc)
target_include_directories(EngineScanVST3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(EngineScanVST3 PUBLIC cxx_std_23)
target_link_libraries(EngineScanVST3 PRIVATE XER_AudioCore XER_Helper)
set_target_properties(EngineScanVST3 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Windows service monitoring removable media for SecurityFlash tokens.
# The service itself only detects token presence; decryption stays with
# the interactive unlock flow that holds the passphrase.
add_executable(SecurityFlashService tools/SecurityFlashService/main.cc)
target_include_directories(SecurityFlashService PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(SecurityFlashService PRIVATE cxx_std_23)
target_link_libraries(SecurityFlashService PRIVATE
    XER_SecurityFlash
    wtsapi32
)
set_target_properties(SecurityFlashService PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# GUI front-end for SecurityFlashService: service control (SCM) plus
# removable-drive/token status, built on the XER UI widget set.
add_executable(ControllAccountsWindows tools/ControllAccountsWindows/main.cc)
target_include_directories(ControllAccountsWindows PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(ControllAccountsWindows PRIVATE cxx_std_23)
target_link_libraries(ControllAccountsWindows PRIVATE
    XER_UI_Core
    user32
    gdi32
    shell32
)
set_target_properties(ControllAccountsWindows PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

add_executable(Editor
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(Editor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(Editor PUBLIC cxx_std_23)

target_link_libraries(Editor
    PUBLIC
        $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
        unofficial::sqlite3::sqlite3
        absl::strings
        absl::synchronization
        absl::flat_hash_map
        absl::flat_hash_set
        unofficial::pthreadpool
        unofficial::fxdiv::fxdiv
        FFTW3::fftw3f
        fmt::fmt
        jsoncpp_lib
        re2::re2
        ZLIB::ZLIB
        range-v3::range-v3
        zstd::libzstd
        unofficial::brotli::brotlienc
        unofficial::brotli::brotlidec
        unofficial::brotli::brotlicommon
        ICU::uc
        ICU::i18n
        OpenSSL::SSL
        OpenSSL::Crypto
        Comctl32
        dbghelp
        shlwapi
        winmm
        psapi
        advapi32
        userenv
        shell32
        version
        ole32
        ws2_32
        crypt32
        windowsapp
        wbemuuid
        XER_Helper
        XER_UI_Core
        XER_UI_Flux
        XER_Flux_Core
        XER_FFmpeg
        XER_AsyncIO
        XER_AudioCore
        XER_ContentAudio
        XER_App
)

target_include_directories(Editor PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(Editor PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(Editor PRIVATE ${FFMPEG_LIBRARIES})
# Compiler flags
if(MSVC)
    target_compile_options(Editor PRIVATE /W4 /WX- /FS)
else()
    target_compile_options(Editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Platform-specific flags
if(WIN32)
    target_compile_definitions(Editor PRIVATE _USE_MATH_DEFINES ENGINE_HAS_FFMPEG_BRIDGE ENGINE_HAS_FFMPEG_AVFILTER ENGINE_HAS_FFMPEG_AVDEVICE)
endif()
 
