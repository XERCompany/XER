# XER GPU Video — CUDA hwaccel decode/encode seams.
#
# YA.CUDADecode / YA.CUDAEncode are runtime-probed modules: no CUDA
# toolkit is needed to build them and no CUDA library is import-linked.
# The NVIDIA driver stack (nvcuda.dll / nvcuvid.dll / nvEncodeAPI64.dll)
# is LoadLibrary-probed at run time and FFmpeg dlopens what it needs
# itself — GPU-less machines get "unavailable" reports, never a loader
# failure. See cuda_runtime.h and nvml_loader.cc for the pattern.
#
# FFmpeg symbols come from the XER_FFmpeg bundle (YA.MediaDecoderUniversal
# under XER_YA_DLLS); video::Frame from YA.VideoFrame / XER_Video.

find_package(libyuv CONFIG QUIET)

set(_gpv_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/cuda_decode.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/cuda_encode.cc)

set(_gpv_deps XER_FFmpeg XER_Helper)
if(TARGET yuv)
    list(APPEND _gpv_deps yuv)
endif()

if(XER_YA_DLLS)
    # video::Frame lives in YA.VideoFrame under the partition build.
    set(_gpv_frame_dep "")
    if(TARGET YA_VideoFrame)
        set(_gpv_frame_dep YA_VideoFrame)
    elseif(TARGET XER_Video)
        set(_gpv_frame_dep XER_Video)
    endif()

    xer_ya_module(YA_CUDADecode EXPORT_NAME YA.CUDADecode
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cuda_decode.cc
        DEPS ${_gpv_deps} ${_gpv_frame_dep})
    xer_ya_module(YA_CUDAEncode EXPORT_NAME YA.CUDAEncode
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cuda_encode.cc
        DEPS ${_gpv_deps} ${_gpv_frame_dep})
    if(TARGET yuv)
        target_compile_definitions(YA_CUDADecode_objs PRIVATE
            XER_GPV_HAS_LIBYUV=1)
        target_compile_definitions(YA_CUDAEncode_objs PRIVATE
            XER_GPV_HAS_LIBYUV=1)
    endif()
    xer_ya_bundle(XER_GpuVideo
        MODULES YA_CUDADecode YA_CUDAEncode
        DEPS ${_gpv_deps} ${_gpv_frame_dep}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
    set(_xer_gpv_scope INTERFACE)
else()
    add_library(XER_GpuVideo STATIC ${_gpv_sources})
    target_include_directories(XER_GpuVideo
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(XER_GpuVideo
        PUBLIC ${_gpv_deps} XER_Video)
    if(TARGET yuv)
        target_compile_definitions(XER_GpuVideo PUBLIC XER_GPV_HAS_LIBYUV=1)
    endif()
    target_compile_features(XER_GpuVideo PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(XER_GpuVideo PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_GpuVideo PRIVATE
            -Wall -Wextra -Wpedantic -O3)
    endif()
endif()

# No delay-load and no staging needed: the only DLLs this module touches
# (nvcuda/nvcuvid/nvEncodeAPI) are LoadLibrary-probed by name at run
# time, and the libav* symbols resolve through YA.MediaDecoderUniversal
# which is already part of every media consumer's runtime closure.
