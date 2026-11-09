# XER SecurityFlash Subsystem CMake Configuration
# USB-bound Windows credential protection: creates local Windows
# accounts and stores credentials as AES-256-GCM encrypted tokens
# on removable flash drives.

file(GLOB SECURITY_FLASH_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB SECURITY_FLASH_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_SecurityFlash STATIC ${SECURITY_FLASH_SOURCES} ${SECURITY_FLASH_HEADERS})

target_include_directories(XER_SecurityFlash
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(XER_SecurityFlash PUBLIC cxx_std_23)

target_link_libraries(XER_SecurityFlash
    PUBLIC
        absl::strings
        absl::str_format
        jsoncpp_lib
        OpenSSL::Crypto
)

if(MSVC)
    target_compile_options(XER_SecurityFlash PRIVATE /W4 /WX- /FS)
else()
    target_compile_options(XER_SecurityFlash PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(XER_SecurityFlash PUBLIC netapi32 advapi32 crypt32)
endif()
