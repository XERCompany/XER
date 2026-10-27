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
