# XER UI Subsystem CMake Configuration

# Collect UI sources
file(GLOB_RECURSE UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
list(FILTER UI_SOURCES EXCLUDE REGEX "/widgets/openai/")
list(FILTER UI_SOURCES EXCLUDE REGEX "/backends/flux/")

file(GLOB_RECURSE UI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
list(FILTER UI_HEADERS EXCLUDE REGEX "/backends/flux/")

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

add_library(XER_UI_Flux STATIC
    backends/flux/flux_renderer.cc
    backends/flux/flux_renderer.h
    backends/flux/flux_input_adapter.cc
    backends/flux/flux_input_adapter.h
)

target_include_directories(XER_UI_Flux
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_UI_Flux
    PUBLIC
        XER_UI_Core
        XER_Flux_Core
)

target_compile_features(XER_UI_Flux PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_UI_Flux PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_UI_Flux PRIVATE -Wall -Wextra -Wpedantic -O3)
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
    # Prevent winsock.h / min-max macros from interfering with std::
    target_compile_definitions(XER_UI_Core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()