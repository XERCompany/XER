# XER UI Subsystem CMake Configuration

# Collect UI sources
file(GLOB_RECURSE UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
list(FILTER UI_SOURCES EXCLUDE REGEX "/widgets/openai/")

file(GLOB_RECURSE UI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create UI core library
add_library(XER_UI_Core STATIC ${UI_SOURCES} ${UI_HEADERS})

target_include_directories(XER_UI_Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link with vcpkg libraries
target_link_libraries(XER_UI_Core
    PUBLIC
        absl::strings
        fmt::fmt
)

# C++ standard
target_compile_features(XER_UI_Core PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_UI_Core PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_UI_Core PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific linking
if(WIN32)
    target_link_libraries(XER_UI_Core
        PUBLIC
            user32
            gdi32
            d2d1
            dxgi
            dwrite
    )
endif()

# Add OpenAI widgets subdirectory
add_subdirectory(widgets/openai)
