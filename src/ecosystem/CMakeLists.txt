# XER Ecosystem Subsystem CMake Configuration
#
# Ecosystem manifest model/parser/loader (JSON-described plugin packages).

file(GLOB ECOSYSTEM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB ECOSYSTEM_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_Ecosystem STATIC ${ECOSYSTEM_SOURCES} ${ECOSYSTEM_HEADERS})

target_include_directories(XER_Ecosystem
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_Ecosystem
    PUBLIC
        absl::strings
        fmt::fmt
        jsoncpp_lib
        range-v3::range-v3
        XER_Helper
)

target_compile_features(XER_Ecosystem PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_Ecosystem PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Ecosystem PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
