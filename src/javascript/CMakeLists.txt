# XER JavaScript Bridge Subsystem CMake Configuration
#
# JS-facing bridge layer: function registration, event routing, pipeline
# execution and common codecs. Requires the V8 engine — only built by
# configurations that provide V8::V8 (ENABLE_V8 builds).

if(ENABLE_V8 AND TARGET V8::V8)
    file(GLOB_RECURSE JS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    )
    file(GLOB_RECURSE JS_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    )

    # src/resource_guard.cc is V8 glue (v8::Isolate) that lives at the src/
    # root — compiled here because this target only exists under ENABLE_V8.
    add_library(XERJavaScript STATIC ${JS_SOURCES} ${JS_HEADERS}
        ../resource_guard.cc ../resource_guard.h)

    target_include_directories(XERJavaScript
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(XERJavaScript
        PUBLIC
            V8::V8
            absl::strings
            absl::synchronization
            absl::hash
            ICU::uc
            ICU::i18n
            $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
            XER_Helper
            XER_AsyncIO
    )

    target_compile_features(XERJavaScript PUBLIC cxx_std_23)
    target_compile_definitions(XERJavaScript PUBLIC
        ENGINE_HAS_V8=1 ENGINE_HAS_LIBUV=1 ENGINE_HAS_ICU=1)

    if(MSVC)
        target_compile_options(XERJavaScript PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XERJavaScript PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
else()
    message(STATUS "V8 not available — XERJavaScript disabled")
endif()
