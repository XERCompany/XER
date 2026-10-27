# XER Core Source CMake Configuration

# Main executable
add_executable(xer main.cc app_command.cc engine_params.cc
               audio_config_helper.cc json_config_helper.cc
               resource_guard.cc feature.cc provider.cc)

# Include directories
target_include_directories(xer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/async_io
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/content
    ${CMAKE_CURRENT_SOURCE_DIR}/flowscript
    ${CMAKE_CURRENT_SOURCE_DIR}/javascript
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime_safety
    ${CMAKE_CURRENT_SOURCE_DIR}/v8
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/lunca
    ${CMAKE_CURRENT_SOURCE_DIR}/native
)

# Link with vcpkg libraries (from parent CMakeLists.txt)
if(TARGET libuv::uv_a OR TARGET libuv::uv)
    target_link_libraries(xer PRIVATE $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
endif()

# Add modular subdirectories
add_subdirectory(async_io)
add_subdirectory(audio)
add_subdirectory(content)
add_subdirectory(ui)
add_subdirectory(lunca)
if(ENABLE_V8)
    add_library(XERJavaScript STATIC
        javascript/bridge/function_bridge.cc
        javascript/common/compression_codec.cc
        javascript/common/text_normalizer.cc
        javascript/common/uv_async_channel.cc
        javascript/engine/runtime_context.cc
        javascript/events/event_router.cc
        javascript/pipeline/pipeline_executor.cc)
    target_include_directories(XERJavaScript PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(XERJavaScript PUBLIC
        ENGINE_HAS_V8=1 ENGINE_HAS_LIBUV=1 ENGINE_HAS_ICU=1)
    target_link_libraries(XERJavaScript PUBLIC V8::V8
        $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
        absl::strings absl::str_format absl::hash)
    target_link_libraries(xer PRIVATE XERJavaScript)
    target_sources(xer PRIVATE
        v8/v8_engine.cc
        v8/v8_exception_handler.cc
        v8/v8_initializer.cc
        v8/v8_memory_manager.cc
        v8/v8_script_runner.cc)
endif()

# Link with XER internal libraries
target_link_libraries(xer PRIVATE
    XER_AsyncIO
    XER_AudioCore
    XER_AudioDSP
    XER_Network
    XER_UI_Core
)

# C++ standard
target_compile_features(xer PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(xer PRIVATE /W4 /WX- /O2 /arch:AVX2)
else()
    target_compile_options(xer PRIVATE -Wall -Wextra -Wpedantic -O3 -march=native)
endif()

# Platform-specific linking
if(WIN32)
    target_link_libraries(xer
        PRIVATE
            ws2_32
            crypt32
            windowsapp
    )
elseif(APPLE)
    target_link_libraries(xer
        PRIVATE
            "-framework CoreFoundation"
            "-framework Foundation"
    )
endif()
