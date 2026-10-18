cmake_minimum_required(VERSION 3.21)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

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

xer_option_from_env(ENABLE_CUDA OFF "Enable CUDA language, CUDA sources, and CUDA bridge targets")
xer_option_from_env(ENABLE_CUDNN OFF "Enable cuDNN sources and bridge targets")
xer_option_from_env(ENABLE_AI OFF "Enable AI and model-builder sources")
xer_option_from_env(ENABLE_OPENCV OFF "Fetch and build OpenCV bridge libraries")
xer_option_from_env(ENABLE_V8 OFF "Build V8 and enable the JavaScript runtime and XER executable")
if(ENABLE_V8)
    set(_xer_build_llvm_default ON)
else()
    set(_xer_build_llvm_default OFF)
endif()
xer_option_from_env(XER_BUILD_LLVM "${_xer_build_llvm_default}" "Build LLVM and Clang from source into .deps")
xer_option_from_env(ENABLE_MEDIA ON "Enable FFmpeg and audio/media dependencies")
option(XER_FETCH_DEPENDENCIES "Download missing CMake dependencies into .deps" ON)
set(XER_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH "Directory for downloaded dependency sources and build trees")
set(FETCHCONTENT_BASE_DIR "${XER_DEPS_DIR}" CACHE PATH "FetchContent source and build directory" FORCE)

if(ENABLE_CUDNN AND NOT ENABLE_CUDA)
    message(FATAL_ERROR "ENABLE_CUDNN=ON requires ENABLE_CUDA=ON")
endif()

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


include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/deps/fetch.cmake)

# ── Dependencies ─────────────────────────────────────────────
include(cmake/deps/system.cmake)
include(cmake/deps/optional.cmake)

# ── Sources ───────────────────────────────────────────────────
include(cmake/sources.cmake)

# ── Targets ───────────────────────────────────────────────────
include(cmake/targets/async_io.cmake)
include(cmake/targets/sql.cmake)
include(cmake/targets/bridges.cmake)
include(cmake/targets/javascript.cmake)
include(cmake/targets/qt6_bridge.cmake)
include(cmake/targets/main.cmake)

if(TARGET XER)
    install(TARGETS XER
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()