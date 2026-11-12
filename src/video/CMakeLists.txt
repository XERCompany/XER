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

add_library(XER_Video STATIC ${VIDEO_SOURCES} ${VIDEO_HEADERS})

target_include_directories(XER_Video
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_Video
    PUBLIC
        absl::strings
        absl::synchronization
        fmt::fmt
        range-v3::range-v3
        XER_Flux_Core
        XER_Cache
        XER_FFmpeg
        XER_AudioCore
        XER_BridgeAngle
        XER_BridgeCuda
        XER_BridgeCudnn
        XER_Helper
)

if(TARGET XER_BridgeSkia)
    target_link_libraries(XER_Video PUBLIC XER_BridgeSkia)
    target_compile_definitions(XER_Video PUBLIC XER_VIDEO_HAS_SKIA=1)
endif()

# Optional GPU/compute headers (vcpkg ports: opencl, VulkanLoader, assimp).
# Sources guard usage with __has_include / runtime probes.
find_package(opencl CONFIG QUIET)
if(TARGET OpenCL::OpenCL)
    target_link_libraries(XER_Video PUBLIC OpenCL::OpenCL OpenCL::Headers)
    target_compile_definitions(XER_Video PUBLIC XER_VIDEO_HAS_OPENCL=1)
endif()

find_package(VulkanLoader CONFIG QUIET)
if(TARGET Vulkan::Loader)
    target_link_libraries(XER_Video PUBLIC Vulkan::Loader)
    target_compile_definitions(XER_Video PUBLIC XER_VIDEO_HAS_VULKAN=1)
endif()

find_package(assimp CONFIG QUIET)
if(TARGET assimp::assimp)
    target_link_libraries(XER_Video PUBLIC assimp::assimp)
    target_compile_definitions(XER_Video PUBLIC XER_VIDEO_HAS_ASSIMP=1)
endif()

target_compile_features(XER_Video PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_Video PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Video PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
