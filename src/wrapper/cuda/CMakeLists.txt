# XER CUDA Engine Bridge — availability probe over the CUDA runtime.
# The library always builds; ENGINE_HAS_CUDA_BRIDGE (and the cudart link)
# only activate when CMake's FindCUDAToolkit locates the toolkit.

add_library(XER_BridgeCuda STATIC
    cuda_engine_bridge.h
    cuda_engine_bridge.cc
)

target_include_directories(XER_BridgeCuda
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_BridgeCuda PUBLIC XER_Helper)

find_package(CUDAToolkit QUIET)
if(TARGET CUDA::cudart)
    target_link_libraries(XER_BridgeCuda PUBLIC CUDA::cudart)
    target_compile_definitions(XER_BridgeCuda PUBLIC ENGINE_HAS_CUDA_BRIDGE=1)
else()
    message(STATUS "CUDA toolkit not found — XER_BridgeCuda reports unavailable")
endif()

target_compile_features(XER_BridgeCuda PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_BridgeCuda PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_BridgeCuda PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
