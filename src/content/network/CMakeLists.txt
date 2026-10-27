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
)

# Link with OpenSSL if available
if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
    target_link_libraries(XER_Network PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

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
endif()
