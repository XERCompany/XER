cmake_minimum_required(VERSION 3.21)

project(XERExamples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(audio_example audio_example.cc)
add_executable(async_io_example async_io_example.cc)
add_executable(cache_example cache_example.cc)
add_executable(network_example network_example.cc)

# Standalone examples; no project-wide link dependencies are required.
target_include_directories(audio_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(async_io_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(cache_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(network_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
