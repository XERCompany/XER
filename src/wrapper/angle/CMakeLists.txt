# XER Angle Engine Bridge — availability probe stub.
# Real ANGEL/GLES interop lands here later; consumers only need IsAvailable().

add_library(XER_BridgeAngle STATIC
    angle_engine_bridge.h
    angle_engine_bridge.cc
)

target_include_directories(XER_BridgeAngle
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_BridgeAngle PUBLIC XER_Helper)

target_compile_features(XER_BridgeAngle PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_BridgeAngle PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_BridgeAngle PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
