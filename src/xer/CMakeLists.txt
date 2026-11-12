# XER Language Subsystem CMake Configuration
#
# The XER language implementation: lexer (xer_tokenzer), AST, compiler,
# runtime values/strings/buffers/bigint, async and sandbox layers, plus the
# sxer84321 encoder under xer/encode.

file(GLOB_RECURSE XER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB_RECURSE XER_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_Xer STATIC ${XER_SOURCES} ${XER_HEADERS})

target_include_directories(XER_Xer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_Xer
    PUBLIC
        absl::strings
        absl::str_format
        absl::flat_hash_map
        absl::hash
        fmt::fmt
        range-v3::range-v3
        ICU::uc
        ICU::i18n
        OpenSSL::SSL
        OpenSSL::Crypto
        XER_Helper
        XER_AsyncIO
        XER_Flux_Core
)

target_compile_features(XER_Xer PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_Xer PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Xer PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
