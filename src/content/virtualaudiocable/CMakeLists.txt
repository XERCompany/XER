# Virtual Audio Cable module: named cross-process PCM cables, WASAPI
# bridges, FX chains (VST3 + builtin) and the shared route engine used
# by XERVac (CLI) and XERVacUI.

file(GLOB VAC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB VAC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(XER_ContentVAC STATIC ${VAC_SOURCES} ${VAC_HEADERS})

target_include_directories(XER_ContentVAC
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio
)

target_link_libraries(XER_ContentVAC
    PUBLIC
        XER_AudioCore
        XER_Helper
        XER_JsonCpp
)

target_compile_features(XER_ContentVAC PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentVAC PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentVAC PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
