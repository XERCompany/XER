# XER Async I/O CMake Configuration

# Collect async_io sources
file(GLOB_RECURSE ASYNC_IO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND ASYNC_IO_SOURCES ${_xer_apple_mm})
endif()
if(NOT ENABLE_CUDNN)
    # cudnn_bridge.cc handles dynamic loading on its own, so it builds with
    # or without cuDNN. Only the cudnn_*_engine.cc files require the linked
    # cuDNN headers and are excluded when ENABLE_CUDNN is OFF.
    list(FILTER ASYNC_IO_SOURCES EXCLUDE REGEX "/dnn_backends/cudnn_[a-z]+_engine\\.cc$")
endif()

file(GLOB_RECURSE ASYNC_IO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create async_io library
add_library(XER_AsyncIO STATIC ${ASYNC_IO_SOURCES} ${ASYNC_IO_HEADERS})

target_include_directories(XER_AsyncIO
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link with vcpkg libraries (jsoncpp is needed for SystemInfoProvider::ToJson).
find_package(jsoncpp CONFIG REQUIRED)

target_link_libraries(XER_AsyncIO
    PUBLIC
        absl::synchronization
        absl::flat_hash_map
        absl::flat_hash_set
        jsoncpp_lib
        XER_Hex
)

# Optional CUDA/cuDNN backend (only when explicitly enabled; the hardware
# abstraction layer loads NVML dynamically so we never hard-depend on it).
if(ENABLE_CUDA OR ENABLE_CUDNN)
    target_link_libraries(XER_AsyncIO PUBLIC CUDA::cudart)
    target_compile_definitions(XER_AsyncIO PRIVATE XER_ASYNC_IO_HAS_CUDA=1)
endif()
if(ENABLE_CUDNN)
    target_link_libraries(XER_AsyncIO PUBLIC CuDNN::CuDNN)
    target_compile_definitions(XER_AsyncIO PRIVATE XER_ASYNC_IO_HAS_CUDNN=1)
endif()

# Optional Vulkan device enumeration.
if(ENABLE_VULKAN)
    target_compile_definitions(XER_AsyncIO PRIVATE XER_ASYNC_IO_HAS_VULKAN=1)
endif()

# Link with libuv
if(TARGET libuv::uv_a OR TARGET libuv::uv)
    target_link_libraries(XER_AsyncIO PUBLIC $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
endif()

# C++ standard
target_compile_features(XER_AsyncIO PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_AsyncIO PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_AsyncIO PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific system libraries.
if(WIN32)
    # ws2_32 + iphlpapi  -> NetworkInfoProvider (GetAdaptersAddresses, inet_ntop)
    # ole32 + wbemuuid   -> TemperatureSensor WMI/ACPI thermal zones (CoInitialize, IWbemLocator)
    target_link_libraries(XER_AsyncIO PUBLIC ws2_32 iphlpapi ole32 wbemuuid)
elseif(APPLE)
    # macOS: no extra libs required for the providers in this module.
else()
    # Linux: nothing extra required (/proc, /sys, getifaddrs are libc).
endif()
