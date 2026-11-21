# Shim config: cuDNN has no MSYS2/apt package — it is a manual NVIDIA SDK
# install. Maps the vcpkg "CUDNN" package name (find_package(CUDNN) +
# CUDNN_FOUND / CUDNN_INCLUDE_DIR / CUDNN_LIBRARY / CuDNN::CuDNN, as used
# by src/wrapper/cudnn and src/content/ai) onto a system install.
#
# Probe order:
#   1. $ENV{CUDNN_ROOT} / $ENV{CUDNN_PATH} — a cuDNN unzip root with
#      include/cudnn.h + lib/ (or lib/x64/).
#   2. The CUDA toolkit itself ($ENV{CUDA_PATH} and the standard
#      C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v* dirs) —
#      NVIDIA's Windows cuDNN zip is meant to be unzipped INTO the
#      toolkit tree, so cudnn.h lands under <toolkit>/include.
#   3. C:/Program Files/NVIDIA/CUDNN/* — standalone install layouts.
#
# MANUAL INSTALL (Windows): download "cuDNN for CUDA 12.x" from
#   https://developer.nvidia.com/cudnn-downloads , unzip, then either
#   copy bin/include/lib into
#   C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.x\ or set
#   CUDNN_ROOT=<unzip dir> in the environment before configuring.
# Linux: apt install libcudnn9-dev-cuda-12 from the NVIDIA repo.
# NOTE: cuDNN is only useful together with the CUDA toolkit
# (CUDAToolkit / ENABLE_CUDA); the cudnn.h headers pull in
# cuda_runtime_api.h.

set(_xer_cudnn_roots "")
foreach(_e CUDNN_ROOT CUDNN_PATH CUDA_PATH CUDA_TOOLKIT_ROOT_DIR)
    if(DEFINED ENV{${_e}} AND IS_DIRECTORY "$ENV{${_e}}")
        list(APPEND _xer_cudnn_roots "$ENV{${_e}}")
    endif()
endforeach()
if(WIN32)
    file(GLOB _xer_cudnn_kits
        "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*")
    list(APPEND _xer_cudnn_roots ${_xer_cudnn_kits})
    file(GLOB _xer_cudnn_standalone
        "C:/Program Files/NVIDIA/CUDNN/*/cuda"
        "C:/Program Files/NVIDIA/CUDNN/*")
    list(APPEND _xer_cudnn_roots ${_xer_cudnn_standalone})
endif()
if(NOT WIN32)
    # Linux NVIDIA repo layout: headers under /usr/include/<arch>-linux-gnu
    # or the toolkit's include dir; both are already default find paths.
    list(APPEND _xer_cudnn_roots /usr/local/cuda /opt/cuda)
endif()

find_path(CUDNN_INCLUDE_DIR
    NAMES cudnn.h
    HINTS ${_xer_cudnn_roots}
    PATH_SUFFIXES include)

find_library(CUDNN_LIBRARY
    NAMES cudnn cudnn64_9 cudnn64_8 cudnn64_7
    HINTS ${_xer_cudnn_roots}
    PATH_SUFFIXES lib lib/x64 lib64 bin)

if(CUDNN_INCLUDE_DIR AND CUDNN_LIBRARY)
    set(CUDNN_FOUND TRUE)
    if(NOT TARGET CuDNN::CuDNN)
        add_library(CuDNN::CuDNN UNKNOWN IMPORTED)
        set_target_properties(CuDNN::CuDNN PROPERTIES
            IMPORTED_LOCATION "${CUDNN_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CUDNN_INCLUDE_DIR}")
    endif()
else()
    set(CUDNN_FOUND FALSE)
endif()

unset(_xer_cudnn_roots)
unset(_xer_cudnn_kits)
unset(_xer_cudnn_standalone)
