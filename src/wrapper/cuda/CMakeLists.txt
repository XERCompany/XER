# XER CUDA Engine Bridge — availability probe stub.
# Real CUDA interop lands here later; consumers only need IsAvailable().

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

target_compile_features(XER_BridgeCuda PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_BridgeCuda PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_BridgeCuda PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
