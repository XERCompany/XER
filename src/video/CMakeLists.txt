# XER Video Subsystem CMake Configuration
#
# Video pipeline: frame model/cache/import-export, compositing canvas,
# render targets, renderers (Skia/ANGLE/CUDA/Vulkan/OpenCL), mask system,
# mix system and the video manager. GPU backends degrade to stubs when the
# matching wrapper bridge reports unavailable.

file(GLOB_RECURSE VIDEO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB_RECURSE VIDEO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(_video_public_deps
    absl::strings
    absl::synchronization
    fmt::fmt
    range-v3::range-v3
    XER_Flux_Core
    XER_Cache
    XER_FFmpeg
    XER_AudioCore
    XER_Helper)

if(XER_YA_DLLS)
    # Leaf-first: the frame model is standalone, the encoder sits on the
    # frame model, and the engine (compositing/mix/masks) calls both.
    set(_ya_video "${VIDEO_SOURCES}")
    xer_ya_partition(_ya_video
        "YA.MediaEncoder|export|encode"
        "YA.MediaGpu|gpu_accel"
        "YA.VideoFrame|/frame|_frame|frame_"
        "YA.VideoCore|.")
    xer_ya_module(YA_VideoFrame EXPORT_NAME YA.VideoFrame
        SOURCES ${XER_YA_SRC_YA.VideoFrame}
        DEPS ${_video_public_deps})
    xer_ya_module(YA_MediaEncoder EXPORT_NAME YA.MediaEncoder
        SOURCES ${XER_YA_SRC_YA.MediaEncoder}
        DEPS ${_video_public_deps} YA_VideoFrame)
    # YA.MediaGpu — GPU clip post-processing leaf (OpenCL/Vulkan accel);
    # XER::Media::AcceleratedVideoDecoder implementations land here too.
    xer_ya_module(YA_MediaGpu EXPORT_NAME YA.MediaGpu
        SOURCES ${XER_YA_SRC_YA.MediaGpu}
        DEPS ${_video_public_deps} YA_VideoFrame)
    xer_ya_module(YA_VideoCore EXPORT_NAME YA.VideoCore
        SOURCES ${XER_YA_SRC_YA.VideoCore} ${VIDEO_HEADERS}
        DEPS ${_video_public_deps} YA_VideoFrame YA_MediaEncoder YA_MediaGpu)
    xer_ya_bundle(XER_Video
        MODULES YA_VideoCore YA_VideoFrame YA_MediaEncoder YA_MediaGpu
        DEPS ${_video_public_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
    set(_xer_video_scope INTERFACE)
else()
    add_library(XER_Video STATIC ${VIDEO_SOURCES} ${VIDEO_HEADERS})
    target_include_directories(XER_Video
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_link_libraries(XER_Video ${_xer_video_scope} ${_video_public_deps})
    set(_xer_video_scope PUBLIC)
endif()

# GPU bridges are optional — each target only exists when its backend
# (ANGLE/CUDA/cuDNN) was found at configure time.
foreach(_bridge IN ITEMS XER_BridgeAngle XER_BridgeCuda XER_BridgeCudnn XER_BridgeOpenCV)
    if(TARGET ${_bridge})
        target_link_libraries(XER_Video ${_xer_video_scope} ${_bridge})
        if(XER_YA_DLLS)
            target_link_libraries(YA_VideoCore PRIVATE ${_bridge})
        endif()
    endif()
endforeach()

if(TARGET XER_BridgeSkia)
    target_link_libraries(XER_Video ${_xer_video_scope} XER_BridgeSkia)
    target_compile_definitions(XER_Video ${_xer_video_scope} XER_VIDEO_HAS_SKIA=1)
    if(XER_YA_DLLS)
        # Skia bridge objects compile/link inside YA.VideoCore itself.
        target_link_libraries(YA_VideoCore PRIVATE XER_BridgeSkia)
        target_link_libraries(YA_VideoCore_objs PRIVATE XER_BridgeSkia)
        target_compile_definitions(YA_VideoCore_objs PRIVATE XER_VIDEO_HAS_SKIA=1)
    endif()
endif()

# Optional GPU/compute headers (vcpkg ports: opencl, VulkanLoader, assimp).
# Sources guard usage with __has_include / runtime probes.
find_package(opencl CONFIG QUIET)
if(TARGET OpenCL::OpenCL)
    target_link_libraries(XER_Video ${_xer_video_scope} OpenCL::OpenCL OpenCL::Headers)
    target_compile_definitions(XER_Video ${_xer_video_scope} XER_VIDEO_HAS_OPENCL=1)
    if(XER_YA_DLLS)
        target_link_libraries(YA_VideoCore PRIVATE OpenCL::OpenCL OpenCL::Headers)
        target_link_libraries(YA_VideoCore_objs PRIVATE OpenCL::Headers)
        target_compile_definitions(YA_VideoCore_objs PRIVATE XER_VIDEO_HAS_OPENCL=1)
        target_link_libraries(YA_MediaGpu PRIVATE OpenCL::OpenCL OpenCL::Headers)
        target_link_libraries(YA_MediaGpu_objs PRIVATE OpenCL::Headers)
        target_compile_definitions(YA_MediaGpu_objs PRIVATE XER_VIDEO_HAS_OPENCL=1)
    endif()
endif()

find_package(VulkanLoader CONFIG QUIET)
if(TARGET Vulkan::Loader)
    target_link_libraries(XER_Video ${_xer_video_scope} Vulkan::Loader)
    target_compile_definitions(XER_Video ${_xer_video_scope} XER_VIDEO_HAS_VULKAN=1)
    if(XER_YA_DLLS)
        target_link_libraries(YA_VideoCore PRIVATE Vulkan::Loader)
        target_compile_definitions(YA_VideoCore_objs PRIVATE XER_VIDEO_HAS_VULKAN=1)
        target_link_libraries(YA_MediaGpu PRIVATE Vulkan::Loader)
        target_link_libraries(YA_MediaGpu_objs PRIVATE Vulkan::Loader)
        target_compile_definitions(YA_MediaGpu_objs PRIVATE XER_VIDEO_HAS_VULKAN=1)
    endif()
endif()

find_package(assimp CONFIG QUIET)
if(TARGET assimp::assimp)
    target_link_libraries(XER_Video ${_xer_video_scope} assimp::assimp)
    target_compile_definitions(XER_Video ${_xer_video_scope} XER_VIDEO_HAS_ASSIMP=1)
    if(XER_YA_DLLS)
        target_link_libraries(YA_VideoCore PRIVATE assimp::assimp)
        target_link_libraries(YA_VideoCore_objs PRIVATE assimp::assimp)
        target_compile_definitions(YA_VideoCore_objs PRIVATE XER_VIDEO_HAS_ASSIMP=1)
    endif()
endif()

if(NOT XER_YA_DLLS)
    target_compile_features(XER_Video PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(XER_Video PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_Video PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
endif()
