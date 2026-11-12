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

find_package(onnxruntime QUIET)
if(TARGET onnxruntime)
    target_link_libraries(XER_ContentAI PUBLIC onnxruntime)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_ONNX=1)
elseif(TARGET ONNX::onnxruntime)
    target_link_libraries(XER_ContentAI PUBLIC ONNX::onnxruntime)
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

# ---------------------------------------------------------------------------
# Additional AI/ML backends. All optional: each block only activates when the
# vcpkg port is installed, and publishes a XER_AI_HAS_<NAME> definition that
# models_builder/utility/ai_runtime_features.cc reports at runtime.
# ---------------------------------------------------------------------------

# Intel MKL: BLAS/LAPACK + FFT + DNN primitives (root locates it REQUIRED).
if(TARGET MKL::MKL)
    target_link_libraries(XER_ContentAI PUBLIC MKL::MKL)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_MKL=1)
endif()

# OpenBLAS: open-source BLAS/LAPACK (alternative to MKL for CPU kernels).
find_package(OpenBLAS QUIET)
if(TARGET OpenBLAS::OpenBLAS)
    target_link_libraries(XER_ContentAI PUBLIC OpenBLAS::OpenBLAS)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_OPENBLAS=1)
endif()

# oneDNN (formerly MKL-DNN): deep-learning primitives for CPU.
find_package(DNNL QUIET)
if(TARGET DNNL::dnnl)
    target_link_libraries(XER_ContentAI PUBLIC DNNL::dnnl)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_DNNL=1)
endif()

# FBGEMM: quantized GEMM/conv/pooling kernels (int8/fp16 paths).
find_package(fbgemmLibrary QUIET)
if(TARGET fbgemm)
    target_link_libraries(XER_ContentAI PUBLIC fbgemm)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_FBGEMM=1)
elseif(TARGET fbgemm_generic)
    target_link_libraries(XER_ContentAI PUBLIC fbgemm_generic)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_FBGEMM=1)
endif()

# cuDNN: GPU-accelerated DNN library (host-side link; kernels need CUDA).
find_package(CUDNN QUIET)
if(TARGET CuDNN::CuDNN)
    target_link_libraries(XER_ContentAI PUBLIC CuDNN::CuDNN)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_CUDNN_LIB=1)
endif()

# cuDNN frontend: modern C++ wrapper API over cuDNN.
find_package(cudnn_frontend QUIET)
if(TARGET cudnn_frontend)
    target_link_libraries(XER_ContentAI PUBLIC cudnn_frontend)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_CUDNN_FRONTEND=1)
endif()

# CUDA toolkit: runtime headers/libs for host-side GPU code paths.
# ml/cuda_ops (.cu kernels) stay excluded unless ENABLE_CUDA is set, which
# also requires the CUDA language enabled project-wide.
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    target_link_libraries(XER_ContentAI PUBLIC CUDA::cudart)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_CUDA_LIB=1)
endif()

# Protocol Buffers: serialization backbone for ONNX/custom op definitions.
find_package(protobuf QUIET)
if(TARGET protobuf::libprotobuf)
    target_link_libraries(XER_ContentAI PUBLIC protobuf::libprotobuf)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_PROTOBUF=1)
endif()

# Tesseract OCR: text-recognition backend for document/image pipelines.
find_package(Tesseract QUIET)
if(TARGET Tesseract::libtesseract)
    target_link_libraries(XER_ContentAI PUBLIC Tesseract::libtesseract)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_TESSERACT=1)
endif()

# yaml-cpp: YAML model/config parsing (complements the libxml2 loader).
find_package(yaml-cpp QUIET)
if(TARGET yaml-cpp::yaml-cpp)
    target_link_libraries(XER_ContentAI PUBLIC yaml-cpp::yaml-cpp)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_YAML_CPP=1)
elseif(TARGET yaml)
    target_link_libraries(XER_ContentAI PUBLIC yaml)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_YAML_CPP=1)
endif()

# Blaze: C++ high-performance math library (alternative to Eigen).
find_package(blaze QUIET)
if(TARGET blaze::blaze)
    target_link_libraries(XER_ContentAI PUBLIC blaze::blaze)
    target_compile_definitions(XER_ContentAI PUBLIC XER_AI_HAS_BLAZE=1)
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
