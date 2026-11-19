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

set(_helper_public_deps
    absl::strings
    range-v3::range-v3
    ZLIB::ZLIB
    XER_JsonCpp
    ICU::uc
    ICU::i18n
    XER_AsyncIO)

if(XER_YA_DLLS)
    list(REMOVE_ITEM HELPER_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/dict.cc")
    xer_ya_module(YA_HelperBase EXPORT_NAME YA.HelperBase
        SOURCES ${HELPER_SOURCES} ${HELPER_HEADERS}
        DEPS ${_helper_public_deps})
    xer_ya_bundle(XER_Helper
        MODULES YA_HelperBase
        DEPS ${_helper_public_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
else()
    add_library(XER_Helper STATIC ${HELPER_SOURCES} ${HELPER_HEADERS})
    target_include_directories(XER_Helper
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_link_libraries(XER_Helper PUBLIC ${_helper_public_deps})
endif()

if(NOT XER_YA_DLLS)
    target_compile_features(XER_Helper PUBLIC cxx_std_23)
endif()
