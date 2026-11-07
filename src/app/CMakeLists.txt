# XER App CMake Configuration

# Collect app sources
file(GLOB APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB APP_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create app library
add_library(XER_App STATIC
    ${APP_SOURCES}
    ${APP_HEADERS}
)

target_include_directories(XER_App
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link with required libraries
target_link_libraries(XER_App
    PUBLIC
        XER_ContentAudio
        XER_AudioCore
        XER_NodeGraph
        XER_Flux_Core
        XER_AsyncIO
        XER_Cache
        XER_UI_Core
        XER_UI_Flux
)

# C++ standard
target_compile_features(XER_App PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_App PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_App PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(XER_App PRIVATE ole32)
endif()
