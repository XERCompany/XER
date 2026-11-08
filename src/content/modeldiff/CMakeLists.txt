# XER Content: 3D model version diff (neutral ModelScene + RE2 search)

file(GLOB MODELDIFF_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB MODELDIFF_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(XER_ContentModelDiff STATIC ${MODELDIFF_SOURCES} ${MODELDIFF_HEADERS})

target_include_directories(XER_ContentModelDiff
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_ContentModelDiff PUBLIC re2::re2)

if(TARGET XER_ContentModel3D)
    target_link_libraries(XER_ContentModelDiff PUBLIC XER_ContentModel3D)
endif()

target_compile_features(XER_ContentModelDiff PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentModelDiff PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentModelDiff PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
