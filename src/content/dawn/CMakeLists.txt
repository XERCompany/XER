# XER Content: Dawn (WebGPU) rendering context + offscreen canvas

file(GLOB DAWN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB DAWN_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(XER_ContentDawn STATIC ${DAWN_SOURCES} ${DAWN_HEADERS})

target_include_directories(XER_ContentDawn
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

find_package(Dawn CONFIG QUIET)
if(TARGET dawn::webgpu_dawn)
    target_link_libraries(XER_ContentDawn PUBLIC
        dawn::webgpu_dawn
        dawn::webgpu_upstream_cpp_headers)
else()
    message(WARNING "Dawn not found — XER_ContentDawn will not build")
    return()
endif()

target_compile_features(XER_ContentDawn PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentDawn PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentDawn PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

if(WIN32)
    target_link_libraries(XER_ContentDawn PRIVATE user32)
endif()
