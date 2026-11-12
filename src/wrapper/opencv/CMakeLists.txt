# XER OpenCV Engine Bridge — availability probe stub.
# Real OpenCV interop lands here later; consumers only need IsAvailable().

add_library(XER_BridgeOpenCV STATIC
    opencv_engine_bridge.h
    opencv_engine_bridge.cc
)

target_include_directories(XER_BridgeOpenCV
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_BridgeOpenCV PUBLIC XER_Helper)

target_compile_features(XER_BridgeOpenCV PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_BridgeOpenCV PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_BridgeOpenCV PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
