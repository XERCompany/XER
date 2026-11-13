# XER Skia Engine Bridge — 2D canvas/codec/effects surface over Skia.
# Optional backend: when the vcpkg skia port is absent the module is skipped
# (same pattern as src/content/model3d with assimp).

find_package(unofficial-skia CONFIG QUIET)

if(TARGET unofficial::skia::skia)
    file(GLOB_RECURSE SKIA_BRIDGE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    )
    file(GLOB_RECURSE SKIA_BRIDGE_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    )

    add_library(XER_BridgeSkia STATIC ${SKIA_BRIDGE_SOURCES} ${SKIA_BRIDGE_HEADERS})

    target_include_directories(XER_BridgeSkia
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
    )

    target_link_libraries(XER_BridgeSkia
        PUBLIC
            unofficial::skia::skia
            XER_Helper
    )

    target_compile_features(XER_BridgeSkia PUBLIC cxx_std_23)
    target_compile_definitions(XER_BridgeSkia PUBLIC ENGINE_HAS_SKIA_BRIDGE=1)

    if(MSVC)
        target_compile_options(XER_BridgeSkia PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_BridgeSkia PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
else()
    message(STATUS "skia not found — XER_BridgeSkia disabled")
endif()
