cmake_minimum_required(VERSION 3.10)
project(pxer_example_plugin C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Output directories for plugin binaries and libraries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
endforeach()

add_library(pxer_example_plugin SHARED pxer_example_plugin.c)

target_include_directories(pxer_example_plugin PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src/native/plugin/pxer
)

target_compile_definitions(pxer_example_plugin PRIVATE PXER_PLUGIN_API_VERSION=1)
