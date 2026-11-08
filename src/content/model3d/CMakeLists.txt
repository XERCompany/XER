# XER Content: 3D model loading (Assimp -> neutral ModelScene)

file(GLOB MODEL3D_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB MODEL3D_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(XER_ContentModel3D STATIC ${MODEL3D_SOURCES} ${MODEL3D_HEADERS})

target_include_directories(XER_ContentModel3D
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

find_package(assimp CONFIG QUIET)
if(TARGET assimp::assimp)
    target_link_libraries(XER_ContentModel3D PUBLIC assimp::assimp)
else()
    message(WARNING "assimp not found — XER_ContentModel3D will not build")
    return()
endif()

target_compile_features(XER_ContentModel3D PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentModel3D PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentModel3D PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
