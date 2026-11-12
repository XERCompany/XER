# XER Content AI Subsystem CMake Configuration
#
# Chromium-style content/ai layer: ML data pipeline (ml/), model
# construction/training/inference (models_builder/), model readers
# (reader/: darknet/onnx/vino/framework adapters) and the sd_base
# diffusion stack.
#
# Heavy numeric backends are optional ports: ggml/ONNX are wired
# here when their CMake configs are present; CUDA ops are excluded
# unless ENABLE_CUDA is set (same rule as src/content/CMakeLists.txt).

file(GLOB_RECURSE AI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND AI_SOURCES ${_xer_apple_mm})
endif()

file(GLOB_RECURSE AI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# CUDA kernels need the CUDA toolkit; keep them out otherwise.
if(NOT ENABLE_CUDA)
    list(FILTER AI_SOURCES EXCLUDE REGEX "/ai/ml/cuda_ops/")
    list(FILTER AI_HEADERS EXCLUDE REGEX "/ai/ml/cuda_ops/")
endif()

add_library(XER_ContentAI STATIC ${AI_SOURCES} ${AI_HEADERS})

target_include_directories(XER_ContentAI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Header-only / config-less vcpkg ports resolve through the toolchain's
# installed include dir; link libraries still need explicit targets.
target_link_libraries(XER_ContentAI
    PUBLIC
        absl::strings
        absl::synchronization
        fmt::fmt
        range-v3::range-v3
        Eigen3::Eigen
        Boost::boost
        cereal::cereal
        OpenSSL::SSL
        OpenSSL::Crypto
        jsoncpp_lib
        ICU::uc
        ICU::i18n
        ZLIB::ZLIB
        zstd::libzstd
        XER_Flux_Core
        XER_Cache
        XER_AsyncIO
        XER_AudioCore
        XER_FFmpeg
        XER_Helper
)

# Optional numeric backends (installed in vcpkg x64-windows-release).
find_package(ggml CONFIG QUIET)
if(TARGET ggml::ggml)
    target_link_libraries(XER_ContentAI PUBLIC ggml::ggml)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_GGML=1)
endif()

find_package(ONNX QUIET)
if(TARGET ONNX::onnx_proto)
    # ONNX::onnx_proto carries the protobuf-generated model definitions
    # (<onnx/onnx_pb.h>) used by the reader/vino adapters.
    target_link_libraries(XER_ContentAI PUBLIC ONNX::onnx_proto)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_ONNX=1)
elseif(TARGET ONNX::onnx)
    target_link_libraries(XER_ContentAI PUBLIC ONNX::onnx)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_ONNX=1)
endif()

# fp16: header-only IEEE half conversion used by ml/utils, onnx_stack and
# reader/transformation weight unpackers.
find_package(unofficial-fp16 CONFIG QUIET)
if(TARGET unofficial::fp16::fp16)
    target_link_libraries(XER_ContentAI PUBLIC unofficial::fp16::fp16)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_FP16=1)
endif()

# pthreadpool: backend for ml/utils/threadpool.h (root already locates it).
if(TARGET unofficial::pthreadpool)
    target_link_libraries(XER_ContentAI PUBLIC unofficial::pthreadpool)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_PTHREADPOOL=1)
endif()

# xnnpack CPU inference backend (guarded by __has_include(<xnnpack.h>)).
find_package(unofficial-xnnpack CONFIG QUIET)
if(TARGET unofficial::xnnpack::XNNPACK)
    target_link_libraries(XER_ContentAI PUBLIC unofficial::xnnpack::XNNPACK)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_XNNPACK=1)
endif()

# libxml2: backend for ml/loaders/loader_yaml.cc.
find_package(LibXml2 QUIET)
if(TARGET LibXml2::LibXml2)
    target_link_libraries(XER_ContentAI PUBLIC LibXml2::LibXml2)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_LIBXML2=1)
endif()

# flatbuffers: probed by models_builder runtime-feature detection.
find_package(flatbuffers CONFIG QUIET)
if(TARGET flatbuffers::flatbuffers)
    target_link_libraries(XER_ContentAI PUBLIC flatbuffers::flatbuffers)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_FLATBUFFERS=1)
endif()

# libuv: probed by runtime-feature detection; AsyncIO already links it.
if(TARGET libuv::uv_a OR TARGET libuv::uv)
    target_link_libraries(XER_ContentAI PUBLIC
        $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_LIBUV=1)
endif()

# OpenCV is used by the image/video loaders and a few trainers.
find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    target_include_directories(XER_ContentAI PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(XER_ContentAI PUBLIC ${OpenCV_LIBS})
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_OPENCV=1)
endif()

target_compile_features(XER_ContentAI PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentAI PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentAI PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

if(WIN32)
    target_link_libraries(XER_ContentAI PRIVATE ws2_32 crypt32 bcrypt)
endif()
