# XER Network Subsystem CMake Configuration

# Collect network sources
file(GLOB_RECURSE NETWORK_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND NETWORK_SOURCES ${_xer_apple_mm})
endif()

file(GLOB_RECURSE NETWORK_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(_network_public_deps
    absl::strings
    absl::synchronization
    fmt::fmt
    XER_JsonCpp
    XER_Hex
    XER_AsyncIO
    XER_Cache
    XER_Flux_Core)

if(XER_YA_DLLS)
    # The network stack is mutually dependent (proxy calls socket
    # platform/logging/async, http/sockets call proxy) — YA.Fetch carries
    # it all; YA.NetworkProxy ships as a reserved extension module.
    xer_ya_module(YA_Fetch EXPORT_NAME YA.Fetch
        SOURCES ${NETWORK_SOURCES} ${NETWORK_HEADERS}
        DEPS ${_network_public_deps} XER_Helper
             OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB)
    xer_ya_module(YA_NetworkProxy EXPORT_NAME YA.NetworkProxy
        DEPS ${_network_public_deps} YA_Fetch)
    if(TARGET CURL::libcurl)
        target_link_libraries(YA_Fetch PRIVATE CURL::libcurl)
        target_link_libraries(YA_Fetch_objs PRIVATE CURL::libcurl)
    endif()
    xer_ya_bundle(XER_Network
        MODULES YA_NetworkProxy YA_Fetch
        DEPS ${_network_public_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/../..)
    set(_xer_net_scope INTERFACE)
else()
    # Create network library
    add_library(XER_Network STATIC ${NETWORK_SOURCES} ${NETWORK_HEADERS})
    target_include_directories(XER_Network
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
    )
    target_link_libraries(XER_Network ${_xer_net_scope} ${_network_public_deps})
endif()

# Link with OpenSSL (required for SSL/TLS)
find_package(OpenSSL REQUIRED)
target_link_libraries(XER_Network ${_xer_net_scope} OpenSSL::SSL OpenSSL::Crypto)

# Link with libcurl if available
if(TARGET CURL::libcurl)
    target_link_libraries(XER_Network ${_xer_net_scope} CURL::libcurl)
endif()

# Link with zlib
target_link_libraries(XER_Network ${_xer_net_scope} ZLIB::ZLIB)

# C++ standard
if(NOT XER_YA_DLLS)
    target_compile_features(XER_Network PUBLIC cxx_std_23)
endif()

# Compiler flags
if(NOT XER_YA_DLLS)
    if(MSVC)
        target_compile_options(XER_Network PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_Network PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
endif()

# Platform-specific
if(WIN32)
    if(XER_YA_DLLS)
        target_link_libraries(YA_Fetch PRIVATE ws2_32 crypt32)
        target_compile_definitions(YA_Fetch_objs PRIVATE WIN32_LEAN_AND_MEAN)
        target_compile_definitions(YA_NetworkProxy_objs PRIVATE WIN32_LEAN_AND_MEAN)
    else()
        target_link_libraries(XER_Network PRIVATE ws2_32 crypt32)
    endif()
    # Prevent winsock.h (v1) from being pulled in via windows.h;
    # network code must use winsock2.h exclusively.
    target_compile_definitions(XER_Network ${_xer_net_scope} WIN32_LEAN_AND_MEAN)
endif()
