cmake_minimum_required(VERSION 3.21)
project(xer_native)

# Set C++ standard (Node.js 26 requires C++20+)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force C++20 for MSVC
if(MSVC)
    add_compile_options(/std:c++20 /permissive-)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake
    ${CMAKE_JS_INC}
    ${CMAKE_CURRENT_SOURCE_DIR}/node_modules/node-addon-api
)

# Source files
set(SOURCES
    src/binding.cc
    ../../src/async_io/async_file_reader.cc
    ../../src/async_io/async_file_writer.cc
    ../../src/async_io/async_buffer_pool.cc
    ../../src/async_io/async_task_queue.cc
    ../../src/async_io/io_buffer_pool.cc
    ../../src/async_io/io_cache_manager.cc
    ../../src/async_io/io_file_map.cc
    ../../src/async_io/io_prefetch_manager.cc
    ../../src/async_io/io_priority_queue.cc
    ../../src/async_io/io_stream_reader.cc
    ../../src/async_io/io_stream_writer.cc
    ../../src/async_io/io_thread_pool.cc
)

# Create shared library
add_library(xer_native SHARED ${SOURCES})

# Set output name
set_target_properties(xer_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "xer_native"
    SUFFIX ".node"
)

# C++20 standard for MSVC
if(MSVC)
    target_compile_options(xer_native PRIVATE /std:c++20 /permissive-)
endif()

# Target definitions
target_compile_definitions(xer_native PRIVATE
    BUILDING_NODE_EXTENSION
)

# Disable warning on Windows
if(WIN32)
    target_compile_definitions(xer_native PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Link against Node.js runtime
target_link_libraries(xer_native PRIVATE ${CMAKE_JS_LIB})

# Export symbols
if(WIN32)
    set_target_properties(xer_native PROPERTIES
        LINK_FLAGS "/EXPORT:Initialize"
    )
endif()

# Set visibility to hidden for Unix-like systems
if(UNIX AND NOT APPLE)
    target_compile_options(xer_native PRIVATE -fvisibility=hidden)
endif()

# Install target
install(TARGETS xer_native
    LIBRARY DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/build/Release
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/build/Release
)
