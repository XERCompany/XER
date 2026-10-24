# ============================================================
# Optional bridge static libraries
# (OpenCV / CUDA / cuDNN / Skia / FFmpeg / ANGLE)
# ============================================================

# ── OpenCV ───────────────────────────────────────────────────
if(ENGINE_WRAPPER_OPENCV_CC)
    add_library(XEROpenCV STATIC ${ENGINE_WRAPPER_OPENCV_CC})
    target_sources(XEROpenCV PRIVATE ${ENGINE_WRAPPER_OPENCV_H})
    target_include_directories(XEROpenCV PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(XEROpenCV PRIVATE
        ENGINE_HAS_OPENCV_BRIDGE=${ENGINE_HAS_OPENCV_BRIDGE})
    if(OpenCV_FOUND)
        target_include_directories(XEROpenCV PRIVATE ${OpenCV_INCLUDE_DIRS})
        target_link_libraries(XEROpenCV PRIVATE ${OpenCV_LIBS})
    endif()
endif()

# ── CUDA ─────────────────────────────────────────────────────
if(ENABLE_CUDA AND ENGINE_WRAPPER_CUDA_CC)
    add_library(XERBridgeCuda STATIC ${ENGINE_WRAPPER_CUDA_CC})
    target_sources(XERBridgeCuda PRIVATE ${ENGINE_WRAPPER_CUDA_H})
    target_include_directories(XERBridgeCuda PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(XERBridgeCuda PRIVATE
        ENGINE_HAS_CUDA_BRIDGE=${ENGINE_HAS_CUDA_BRIDGE})
    if(CUDAToolkit_FOUND)
        target_include_directories(XERBridgeCuda PRIVATE
            ${CUDAToolkit_INCLUDE_DIRS})
        if(TARGET CUDA::cudart)
            target_link_libraries(XERBridgeCuda PRIVATE CUDA::cudart)
        endif()
    endif()
endif()

# ── cuDNN ────────────────────────────────────────────────────
if(ENABLE_CUDNN AND ENGINE_WRAPPER_CUDNN_CC)
    add_library(XERBridgeCudnn STATIC ${ENGINE_WRAPPER_CUDNN_CC})
    target_sources(XERBridgeCudnn PRIVATE ${ENGINE_WRAPPER_CUDNN_H})
    target_include_directories(XERBridgeCudnn PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(XERBridgeCudnn PRIVATE
        ENGINE_HAS_CUDNN_BRIDGE=${ENGINE_HAS_CUDNN_BRIDGE})
    if(CUDNN_INCLUDE_DIR)
        target_include_directories(XERBridgeCudnn PRIVATE ${CUDNN_INCLUDE_DIR})
    endif()
    if(CUDNN_LIBRARY)
        target_link_libraries(XERBridgeCudnn PRIVATE ${CUDNN_LIBRARY})
    endif()
endif()

# ── Skia ─────────────────────────────────────────────────────
if(ENGINE_WRAPPER_SKIA_CC)
    add_library(XERBridgeSkia STATIC ${ENGINE_WRAPPER_SKIA_CC})
    target_sources(XERBridgeSkia PRIVATE ${ENGINE_WRAPPER_SKIA_H})
    target_include_directories(XERBridgeSkia PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(XERBridgeSkia PRIVATE
        ENGINE_HAS_SKIA_BRIDGE=${ENGINE_HAS_SKIA_BRIDGE})
    if(ENGINE_SKIA_INCLUDE_DIRS)
        target_include_directories(XERBridgeSkia PRIVATE
            ${ENGINE_SKIA_INCLUDE_DIRS})
    endif()
    if(ENGINE_SKIA_LIBRARIES)
        target_link_libraries(XERBridgeSkia PRIVATE ${ENGINE_SKIA_LIBRARIES})
    endif()
endif()

# ── FFmpeg ───────────────────────────────────────────────────
if(ENGINE_WRAPPER_FFMPEG_CC)
    add_library(XERBridgeFfmpeg STATIC ${ENGINE_WRAPPER_FFMPEG_CC})
    target_sources(XERBridgeFfmpeg PRIVATE ${ENGINE_WRAPPER_FFMPEG_H})
    target_include_directories(XERBridgeFfmpeg PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(XERBridgeFfmpeg PRIVATE
        ENGINE_HAS_FFMPEG_BRIDGE=${ENGINE_HAS_FFMPEG_BRIDGE}
        ENGINE_HAS_FFMPEG_AVFILTER=${ENGINE_HAS_FFMPEG_AVFILTER}
        ENGINE_HAS_FFMPEG_AVDEVICE=${ENGINE_HAS_FFMPEG_AVDEVICE})
    if(ENGINE_FFMPEG_INCLUDE_DIRS)
        target_include_directories(XERBridgeFfmpeg PRIVATE
            ${ENGINE_FFMPEG_INCLUDE_DIRS})
    endif()
    target_link_libraries(XERBridgeFfmpeg PRIVATE absl::str_format)
    if(ENGINE_FFMPEG_LIBRARIES)
        target_link_libraries(XERBridgeFfmpeg PRIVATE ${ENGINE_FFMPEG_LIBRARIES})
    endif()
    if(TARGET XERFFmpegExternal)
        add_dependencies(XERBridgeFfmpeg XERFFmpegExternal)
    endif()
endif()

# ── ANGLE ────────────────────────────────────────────────────
if(ENGINE_WRAPPER_ANGLE_CC)
    add_library(XERBridgeAngle STATIC ${ENGINE_WRAPPER_ANGLE_CC})
    target_sources(XERBridgeAngle PRIVATE ${ENGINE_WRAPPER_ANGLE_H})
    target_include_directories(XERBridgeAngle PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(XERBridgeAngle PRIVATE
        ENGINE_HAS_ANGLE_BRIDGE=${ENGINE_HAS_ANGLE_BRIDGE})
    if(ANGLE_INCLUDE_DIR)
        target_include_directories(XERBridgeAngle PRIVATE ${ANGLE_INCLUDE_DIR})
    endif()
    if(ANGLE_EGL_LIBRARY)
        target_link_libraries(XERBridgeAngle PRIVATE ${ANGLE_EGL_LIBRARY})
    endif()
    if(ANGLE_GLESV2_LIBRARY)
        target_link_libraries(XERBridgeAngle PRIVATE ${ANGLE_GLESV2_LIBRARY})
    endif()
endif()

# ── OpenAI ───────────────────────────────────────────────────
# Collect OpenAI sources
file(GLOB_RECURSE ENGINE_WRAPPER_OPENAI_CC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/content/openai/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/content/openai/**/*.cc"
)

file(GLOB_RECURSE ENGINE_WRAPPER_OPENAI_H
    "${CMAKE_CURRENT_SOURCE_DIR}/src/content/openai/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/content/openai/**/*.h"
)

if(ENGINE_WRAPPER_OPENAI_CC)
    add_library(XERBridgeOpenAI STATIC ${ENGINE_WRAPPER_OPENAI_CC})
    target_sources(XERBridgeOpenAI PRIVATE ${ENGINE_WRAPPER_OPENAI_H})
    target_include_directories(XERBridgeOpenAI PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(XERBridgeOpenAI PRIVATE
        ENGINE_HAS_OPENAI_BRIDGE=${ENGINE_HAS_OPENAI_BRIDGE})
    target_link_libraries(XERBridgeOpenAI
        PRIVATE
        XER_Network
        XER_AsyncIO
        XER_AudioCore
        absl::strings
        absl::synchronization
        absl::container
        range_v3
        re2::re2
    )
    if(OPENSSL_FOUND)
        target_link_libraries(XERBridgeOpenAI PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
    if(ZLIB_FOUND)
        target_link_libraries(XERBridgeOpenAI PRIVATE ZLIB::ZLIB)
    endif()
    if(ZSTD_FOUND)
        target_link_libraries(XERBridgeOpenAI PRIVATE zstd_static)
    endif()
endif()
