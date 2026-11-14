# Content Video Decoder CMake Configuration

find_package(FFMPEG REQUIRED)
find_package(Dawn CONFIG QUIET)
find_package(libavif CONFIG QUIET)
find_package(libyuv CONFIG QUIET)

file(GLOB VIDEO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND VIDEO_SOURCES ${_xer_apple_mm})
endif()

file(GLOB VIDEO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_ContentVideo STATIC
    ${VIDEO_SOURCES}
    ${VIDEO_HEADERS}
)

target_include_directories(XER_ContentVideo
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${FFMPEG_INCLUDE_DIRS}
)

# vcpkg include root for ports without CMake config packages (x264/x265/lerc).
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    target_include_directories(XER_ContentVideo
        PUBLIC
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
    )
endif()

# Encoder/compression libs ship without CMake config packages — resolve to
# absolute paths so dependents don't need the vcpkg lib dir on their link line.
# On Linux/macOS they come from the system (libx264-dev, libx265-dev, liblerc-dev)
# via the default library search path.
set(_xer_video_lib_paths "")
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _xer_video_lib_paths "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
endif()
find_library(X264_LIB NAMES libx264 x264 PATHS ${_xer_video_lib_paths})
find_library(X265_LIB NAMES libx265 x265 PATHS ${_xer_video_lib_paths})
find_library(LERC_LIB NAMES Lerc lerc PATHS ${_xer_video_lib_paths})

set(_xer_video_encoders "")
foreach(_enc_lib IN ITEMS ${X264_LIB} ${X265_LIB} ${LERC_LIB})
    if(_enc_lib AND NOT _enc_lib MATCHES "-NOTFOUND$")
        list(APPEND _xer_video_encoders "${_enc_lib}")
    endif()
endforeach()

set(_xer_video_links
    XER_FFmpeg       # engine::bridge::ffmpeg — all FFmpeg/libyuv usage
    XER_Helper       # string/path helpers (also via XER_FFmpeg, explicit)
    avif             # libavif (AVIF stills)
    ${_xer_video_encoders}
)
# Dawn/WebGPU backend is optional — absent on system-package builds.
if(TARGET dawn::webgpu_dawn)
    list(APPEND _xer_video_links
        dawn::webgpu_dawn dawn::dawn_public_config dawn::webgpu_upstream_cpp_headers)
endif()
target_link_libraries(XER_ContentVideo PUBLIC ${_xer_video_links})

target_compile_features(XER_ContentVideo PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentVideo PRIVATE /W4 /WX- /O2)
    target_compile_definitions(XER_ContentVideo PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(XER_ContentVideo PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
