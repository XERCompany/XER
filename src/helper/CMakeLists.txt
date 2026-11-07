# XER Helper Subsystem CMake Configuration
# Shared string/buffer/dict/file utilities (helper/*.h).

file(GLOB HELPER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND HELPER_SOURCES ${_xer_apple_mm})
endif()
file(GLOB HELPER_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_Helper STATIC ${HELPER_SOURCES} ${HELPER_HEADERS})

target_include_directories(XER_Helper
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(XER_Helper PUBLIC cxx_std_23)

target_link_libraries(XER_Helper
    PUBLIC
        absl::strings
        range-v3::range-v3
        ZLIB::ZLIB
        jsoncpp_lib
        ICU::uc
        ICU::i18n
        XER_AsyncIO
)
