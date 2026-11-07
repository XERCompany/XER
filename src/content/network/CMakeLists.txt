# XER Network Subsystem CMake Configuration

# Collect network sources
file(GLOB_RECURSE NETWORK_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE NETWORK_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create network library
add_library(XER_Network STATIC ${NETWORK_SOURCES} ${NETWORK_HEADERS})

target_include_directories(XER_Network
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Link with vcpkg libraries
target_link_libraries(XER_Network
    PUBLIC
        absl::strings
        absl::synchronization
        fmt::fmt
        jsoncpp_lib
        XER_Hex
)

# Link with internal modules
target_link_libraries(XER_Network
    PUBLIC
        XER_AsyncIO
        XER_Cache
        XER_Flux_Core
)

# Link with OpenSSL (required for SSL/TLS)
find_package(OpenSSL REQUIRED)
target_link_libraries(XER_Network PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# Link with libcurl if available
if(TARGET CURL::libcurl)
    target_link_libraries(XER_Network PUBLIC CURL::libcurl)
endif()

# Link with zlib
target_link_libraries(XER_Network PUBLIC ZLIB::ZLIB)

# C++ standard
target_compile_features(XER_Network PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_Network PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Network PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(XER_Network PRIVATE ws2_32 crypt32)
    # Prevent winsock.h (v1) from being pulled in via windows.h;
    # network code must use winsock2.h exclusively.
    target_compile_definitions(XER_Network PUBLIC WIN32_LEAN_AND_MEAN)
endif()
