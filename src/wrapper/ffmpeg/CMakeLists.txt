# XER FFmpeg Wrapper CMake Configuration
# All libav*/libyuv usage is confined to this directory. Consumers
# (XER_AudioCore, XER_ContentVideo, XER_App, Editor) link XER_FFmpeg and use
# the engine::bridge::ffmpeg API — they never include FFmpeg headers.

find_package(FFMPEG REQUIRED)
find_package(libyuv CONFIG QUIET)

set(_ffmpeg_bridge_srcs
    ffmpeg_engine_bridge.h
    ffmpeg_engine_bridge.cc
    ffmpeg_video_decoder.h
    ffmpeg_video_decoder.cc)
set(_ffmpeg_bridge_includes
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${FFMPEG_INCLUDE_DIRS})

if(XER_YA_DLLS AND XER_FFMPEG_API_ARCHIVES)
    # YA.MediaDecoderUniversal: the FFmpeg static closure + the bridge
    # code packed into one branded shared library. FFMPEG_LIBRARIES
    # already points at this target for every consumer.
    add_library(XER_FFmpeg_impl STATIC ${_ffmpeg_bridge_srcs})
    target_include_directories(XER_FFmpeg_impl PUBLIC ${_ffmpeg_bridge_includes})
    target_link_libraries(XER_FFmpeg_impl PUBLIC absl::strings XER_Helper)
    target_compile_features(XER_FFmpeg_impl PUBLIC cxx_std_23)
    xer_add_ya_dll(YA_MediaDecoderUniversal
        EXPORT_NAME YA.MediaDecoderUniversal
        API_ARCHIVES ${XER_FFMPEG_API_ARCHIVES}
        API_TARGETS XER_FFmpeg_impl
        ARCHIVES ${XER_FFMPEG_DEP_ARCHIVES}
        LIBS YA_LibXml2 ${XER_FFMPEG_DYN_LIBS}
             absl::strings yuv XER_Helper)
    # Delay-load .dll.a-only deps (whisper/ggml/shaderc/vulkan): the
    # module — and every exe depending on it — starts without those
    # MSYS2 DLLs present; they load on first actual use.
    if(XER_FFMPEG_DELAYLOAD_DLLS)
        set(_ff_dl_opts)
        foreach(_d IN LISTS XER_FFMPEG_DELAYLOAD_DLLS)
            list(APPEND _ff_dl_opts "-Wl,--delayload,${_d}")
        endforeach()
        target_link_options(YA_MediaDecoderUniversal PRIVATE ${_ff_dl_opts})
        message(STATUS
            "FFMPEG: delay-loaded DLLs: ${XER_FFMPEG_DELAYLOAD_DLLS}")
    endif()
    xer_ya_bundle(XER_FFmpeg
        MODULES YA_MediaDecoderUniversal
        DEPS absl::strings yuv XER_Helper
        INCLUDES ${_ffmpeg_bridge_includes})
    target_link_directories(XER_FFmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
    target_compile_definitions(XER_FFmpeg INTERFACE
        ENGINE_HAS_FFMPEG_BRIDGE
        ENGINE_HAS_FFMPEG_AVFILTER
        ENGINE_HAS_FFMPEG_AVDEVICE)
else()
    add_library(XER_FFmpeg STATIC ${_ffmpeg_bridge_srcs})
    target_include_directories(XER_FFmpeg PUBLIC ${_ffmpeg_bridge_includes})
endif()

# vcpkg include root for ports without CMake config packages.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    target_include_directories(XER_FFmpeg
        PUBLIC
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
    )
endif()

if(NOT XER_YA_DLLS)
    target_link_libraries(XER_FFmpeg
        PUBLIC
            ${FFMPEG_LIBRARIES}
            absl::strings
            yuv            # libyuv (libyuv-config)
            XER_Helper
    )

    # avfilter/avdevice ship inside FFMPEG_LIBRARIES on vcpkg; keep the
    # libs discoverable for consumers that only see this interface.
    target_link_directories(XER_FFmpeg PUBLIC ${FFMPEG_LIBRARY_DIRS})

    target_compile_definitions(XER_FFmpeg PUBLIC
        ENGINE_HAS_FFMPEG_BRIDGE
        ENGINE_HAS_FFMPEG_AVFILTER
        ENGINE_HAS_FFMPEG_AVDEVICE)
endif()

if(NOT XER_YA_DLLS)
    target_compile_features(XER_FFmpeg PUBLIC cxx_std_23)
endif()

if(NOT XER_YA_DLLS)
    if(MSVC)
        target_compile_options(XER_FFmpeg PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_FFmpeg PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
endif()
