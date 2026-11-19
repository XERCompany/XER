# XER::Media — media pipeline (see "MediaDecoder&Encoder -.txt").
#
# Module map (branded YA shared libraries):
#   YA.MediaCore     — interfaces + core impls: types/logging/buffers,
#                      demuxers, session/audio-focus
#   YA.MediaPipeline — decoders, renderers, sinks, player, software
#                      decoder filters (FFmpeg adapters)
#   YA.MediaRouter   — route bookkeeping + providers (Presentation API)
#   YA.MediaMuxer    — container muxers (webm/mkv/mp4) + timestamp adapter
#   YA.MediaCapture  — OS/self audio+pixel capture
#   YA.MediaAudio    — Audio Service layer: AudioManager, streams,
#                      dispatcher/proxy/resampler, device thread,
#                      shared-memory audio segment
#   YA.MediaGpu      — accelerated decoders + clip GPU accel (lives in
#                      src/video; media/gpu sources merge into it)
#   YA.DesginCSS     — reserved: XER::Content::CSS style engine
#
# Legacy aggregate: XER_Media.

file(GLOB_RECURSE MEDIA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB_RECURSE MEDIA_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

if(XER_YA_DLLS)
    set(_ya_media "${MEDIA_SOURCES}")
    xer_ya_partition(_ya_media
        "YA.MediaPipeline|/decoders/|/renderers/|/sinks/|/player/|/filters/"
        "YA.MediaRouter|/router/"
        "YA.MediaMuxer|/muxers/"
        "YA.MediaCapture|/capture/"
        "YA.MediaAudio|/audio/|/service/"
        "YA.MediaCore|.")

    set(_ya_dir "${CMAKE_BINARY_DIR}/ya")

    xer_ya_module(YA_MediaCore EXPORT_NAME YA.MediaCore
        SOURCES ${XER_YA_SRC_YA.MediaCore} ${MEDIA_HEADERS}
        DEPS XER_Helper XER_AsyncIO XER_Network XER_Flux_Core)
    xer_ya_module(YA_MediaAudio EXPORT_NAME YA.MediaAudio
        SOURCES ${XER_YA_SRC_YA.MediaAudio}
        DEPS XER_Helper XER_Cache XER_AudioCore YA_MediaCore)
    xer_ya_module(YA_MediaPipeline EXPORT_NAME YA.MediaPipeline
        SOURCES ${XER_YA_SRC_YA.MediaPipeline}
        DEPS XER_Helper YA_MediaCore YA_MediaAudio XER_FFmpeg XER_Video)
    xer_ya_module(YA_MediaRouter EXPORT_NAME YA.MediaRouter
        SOURCES ${XER_YA_SRC_YA.MediaRouter}
        DEPS XER_Helper YA_MediaCore)
    xer_ya_module(YA_MediaMuxer EXPORT_NAME YA.MediaMuxer
        SOURCES ${XER_YA_SRC_YA.MediaMuxer}
        DEPS XER_Helper YA_MediaCore XER_FFmpeg)
    xer_ya_module(YA_MediaCapture EXPORT_NAME YA.MediaCapture
        SOURCES ${XER_YA_SRC_YA.MediaCapture}
        DEPS XER_Helper YA_MediaCore YA_MediaAudio)

    # YA.DesginCSS — XER::UI::CSS style engine (lives in src/ui/css per
    # the design doc: style calculation belongs to Desgin UI).
    file(GLOB _css_sources
        "${CMAKE_SOURCE_DIR}/src/ui/css/*.cc")
    xer_ya_module(YA_DesginCSS EXPORT_NAME YA.DesginCSS
        SOURCES ${_css_sources}
        DEPS XER_Helper)
    if(TARGET YA_DesginCSS_objs)
        target_include_directories(YA_DesginCSS_objs PRIVATE
            ${CMAKE_SOURCE_DIR}/src)
    endif()

    xer_ya_bundle(XER_Media
        MODULES YA_MediaPipeline YA_MediaCore YA_MediaAudio
                YA_MediaRouter YA_MediaCapture YA_MediaMuxer YA_DesginCSS
        DEPS XER_Helper
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
else()
    add_library(XER_Media STATIC ${MEDIA_SOURCES} ${MEDIA_HEADERS})
    target_include_directories(XER_Media PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(XER_Media PUBLIC XER_Helper XER_Cache XER_FFmpeg
        XER_AsyncIO XER_Network XER_AudioCore XER_Video)
    target_compile_features(XER_Media PUBLIC cxx_std_23)
endif()

# media/gpu — accelerated decoder implementations merge into the existing
# YA.MediaGpu module owned by src/video (clip_gpu_accel lives there).
if(XER_YA_DLLS AND TARGET YA_MediaGpu_objs)
    file(GLOB _media_gpu_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/gpu/*.cc")
    if(_media_gpu_sources)
        target_sources(YA_MediaGpu_objs PRIVATE ${_media_gpu_sources})
        target_include_directories(YA_MediaGpu_objs PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/..)
        target_link_libraries(YA_MediaGpu PRIVATE YA_MediaCore)
    endif()
endif()
