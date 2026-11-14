file(GLOB_RECURSE FLUX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND FLUX_SOURCES ${_xer_apple_mm})
endif()

file(GLOB_RECURSE FLUX_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_Flux_Core STATIC ${FLUX_SOURCES} ${FLUX_HEADERS})

target_include_directories(XER_Flux_Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_Flux_Core
    PUBLIC
        absl::strings
)

target_compile_features(XER_Flux_Core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_Flux_Core PRIVATE /W4 /WX- /FS)
else()
    target_compile_options(XER_Flux_Core PRIVATE -Wall -Wextra -Wpedantic)
endif()
