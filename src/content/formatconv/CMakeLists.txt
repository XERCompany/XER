# XER Content: batch 3D-format converter (Assimp + AsyncIO thread pool)

file(GLOB FORMATIC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB FORMATIC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(XER_ContentFormatConv STATIC ${FORMATIC_SOURCES} ${FORMATIC_HEADERS})

target_include_directories(XER_ContentFormatConv
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

find_package(assimp CONFIG QUIET)
if(TARGET assimp::assimp)
    target_link_libraries(XER_ContentFormatConv PUBLIC assimp::assimp)
else()
    message(WARNING "assimp not found — XER_ContentFormatConv will not build")
    return()
endif()

if(TARGET XER_AsyncIO)
    target_link_libraries(XER_ContentFormatConv PUBLIC XER_AsyncIO)
endif()

target_compile_features(XER_ContentFormatConv PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentFormatConv PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentFormatConv PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
