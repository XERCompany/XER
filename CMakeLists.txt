# ============================================================
# XER - High-Performance C++/V8 Runtime Builder
# ============================================================
# CMake minimum version requirement
cmake_minimum_required(VERSION 3.21)

# Add custom CMake modules directory to search path
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# ============================================================
# Helper Functions
# ============================================================

# Function to create a CMake option that can be overridden by environment variable
# Usage: xer_option_from_env(OPTION_NAME DEFAULT "Description")
function(xer_option_from_env option_name default_value description)
    set(option_default "${default_value}")
    if(DEFINED ENV{${option_name}})
        string(TOUPPER "$ENV{${option_name}}" option_env_value)
        if(option_env_value MATCHES "^(1|ON|TRUE|YES|Y)$")
            set(option_default ON)
        elseif(option_env_value MATCHES "^(0|OFF|FALSE|NO|N)$")
            set(option_default OFF)
        endif()
    endif()
    option(${option_name} "${description}" ${option_default})
endfunction()

# ============================================================
# Project Options
# ============================================================

# GPU and AI support options
xer_option_from_env(ENABLE_CUDA OFF "Enable CUDA language, CUDA sources, and CUDA bridge targets")
xer_option_from_env(ENABLE_CUDNN OFF "Enable cuDNN sources and bridge targets")
xer_option_from_env(ENABLE_AI OFF "Enable AI and model-builder sources")

# Multimedia and vision options
xer_option_from_env(ENABLE_OPENCV OFF "Fetch and build OpenCV bridge libraries")
xer_option_from_env(ENABLE_MEDIA ON "Enable FFmpeg and audio/media dependencies")

# JavaScript runtime options
xer_option_from_env(ENABLE_V8 ON "Build V8 and enable the JavaScript runtime and XER executable")
xer_option_from_env(XER_BUILD_LLVM OFF "Build LLVM and Clang from source into .deps (WARNING: takes 10-30 min)")

# V8 build options (only used when ENABLE_V8=ON)
option(XER_SKIP_V8_SYNC "Skip V8 source sync if already present (useful for cached V8 builds)" OFF)

# Dependency management options
option(XER_FETCH_DEPENDENCIES "Download missing CMake dependencies into .deps" ON)
set(XER_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH "Directory for downloaded dependency sources and build trees")
set(FETCHCONTENT_BASE_DIR "${XER_DEPS_DIR}" CACHE PATH "FetchContent source and build directory" FORCE)

# ============================================================
# Dependency Validation
# ============================================================

# cuDNN requires CUDA
if(ENABLE_CUDNN AND NOT ENABLE_CUDA)
    message(FATAL_ERROR "ENABLE_CUDNN=ON requires ENABLE_CUDA=ON")
endif()

# Check CUDA availability if enabled
if(ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(NOT CMAKE_CUDA_COMPILER)
        message(FATAL_ERROR "ENABLE_CUDA=ON requires a CUDA toolkit and a supported CUDA compiler")
    endif()
    set(CMAKE_CUDA_STANDARD 17)
    project(XER LANGUAGES C CXX CUDA)
else()
    project(XER LANGUAGES C CXX)
endif()

# ============================================================
# Project Configuration
# ============================================================

# Standard installation directories
include(GNUInstallDirs)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Dependency Management
# ============================================================

include(cmake/deps/fetch.cmake)

# System dependencies
include(cmake/deps/system.cmake)

# Optional dependencies
include(cmake/deps/optional.cmake)

# ============================================================
# Source Files
# ============================================================

include(cmake/sources.cmake)

# ============================================================
# Build Targets
# ============================================================

# Async I/O library
include(cmake/targets/async_io.cmake)

# SQL support
include(cmake/targets/sql.cmake)

# Multimedia bridges
include(cmake/targets/bridges.cmake)

# WinAPI bridge (platform-specific)
include(cmake/targets/winapi_bridge.cmake)

# JavaScript runtime
include(cmake/targets/javascript.cmake)

# Qt6 bridge (optional)
include(cmake/targets/qt6_bridge.cmake)

# Main executable
include(cmake/targets/main.cmake)

# ============================================================
# Installation
# ============================================================

if(TARGET XER)
    install(TARGETS XER
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()