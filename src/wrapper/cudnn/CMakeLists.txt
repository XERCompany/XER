# XER cuDNN Engine Bridge — availability probe over the cuDNN library.
# The library always builds; ENGINE_HAS_CUDNN_BRIDGE (and the cudnn link)
# only activate when FindCUDNN locates cudnn.h + cudnn.lib.

add_library(XER_BridgeCudnn STATIC
    cudnn_engine_bridge.h
    cudnn_engine_bridge.cc
)

target_include_directories(XER_BridgeCudnn
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_BridgeCudnn PUBLIC XER_Helper)

find_package(CUDNN QUIET)
# cudnn.h includes cuda_runtime_api.h — pull in the CUDA toolkit headers too.
find_package(CUDAToolkit QUIET)
if(CUDNN_FOUND)
    target_include_directories(XER_BridgeCudnn PRIVATE ${CUDNN_INCLUDE_DIR})
    if(TARGET CUDA::cudart)
        target_link_libraries(XER_BridgeCudnn PUBLIC CUDA::cudart)
        target_include_directories(XER_BridgeCudnn PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
    endif()
    target_link_libraries(XER_BridgeCudnn PUBLIC ${CUDNN_LIBRARY})
    target_compile_definitions(XER_BridgeCudnn PUBLIC ENGINE_HAS_CUDNN_BRIDGE=1)
else()
    message(STATUS "cuDNN not found — XER_BridgeCudnn reports unavailable")
endif()

target_compile_features(XER_BridgeCudnn PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_BridgeCudnn PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_BridgeCudnn PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
