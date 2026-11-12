# XER cuDNN Engine Bridge — availability probe stub.
# Real cuDNN interop lands here later; consumers only need IsAvailable().

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

target_compile_features(XER_BridgeCudnn PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_BridgeCudnn PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_BridgeCudnn PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
