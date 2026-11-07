# Content Clipboard CMake Configuration

file(GLOB CLIPBOARD_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB CLIPBOARD_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_ContentClipboard STATIC
    ${CLIPBOARD_SOURCES}
    ${CLIPBOARD_HEADERS}
)

target_include_directories(XER_ContentClipboard
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_compile_features(XER_ContentClipboard PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentClipboard PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentClipboard PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

if(WIN32)
    target_link_libraries(XER_ContentClipboard PRIVATE user32)
endif()
