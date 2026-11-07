# XER FFmpeg Wrapper CMake Configuration
# All libav*/libyuv usage is confined to this directory. Consumers
# (XER_AudioCore, XER_ContentVideo, XER_App, Editor) link XER_FFmpeg and use
# the engine::bridge::ffmpeg API — they never include FFmpeg headers.

find_package(FFMPEG REQUIRED)
find_package(libyuv CONFIG QUIET)

add_library(XER_FFmpeg STATIC
    ffmpeg_engine_bridge.h
    ffmpeg_engine_bridge.cc
    ffmpeg_video_decoder.h
    ffmpeg_video_decoder.cc
)

target_include_directories(XER_FFmpeg
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${FFMPEG_INCLUDE_DIRS}
)

# vcpkg include root for ports without CMake config packages.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    target_include_directories(XER_FFmpeg
        PUBLIC
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
    )
endif()

target_link_libraries(XER_FFmpeg
    PUBLIC
        ${FFMPEG_LIBRARIES}
        absl::strings
        yuv            # libyuv (libyuv-config)
        XER_Helper
)

# avfilter/avdevice ship inside FFMPEG_LIBRARIES on vcpkg; keep the libs
# discoverable for consumers that only see this interface target.
target_link_directories(XER_FFmpeg PUBLIC ${FFMPEG_LIBRARY_DIRS})

target_compile_features(XER_FFmpeg PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_FFmpeg PRIVATE /W4 /WX- /O2)
    target_compile_definitions(XER_FFmpeg PUBLIC
        ENGINE_HAS_FFMPEG_BRIDGE
        ENGINE_HAS_FFMPEG_AVFILTER
        ENGINE_HAS_FFMPEG_AVDEVICE)
else()
    target_compile_options(XER_FFmpeg PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
