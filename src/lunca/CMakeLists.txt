# XER Lunca Audio Streaming CMake Configuration

# Collect lunca sources
file(GLOB_RECURSE LUNCA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE LUNCA_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create lunca library
add_library(XER_Lunca STATIC ${LUNCA_SOURCES} ${LUNCA_HEADERS})

target_include_directories(XER_Lunca
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link with vcpkg libraries
target_link_libraries(XER_Lunca
    PUBLIC
        absl::strings
        absl::synchronization
        fmt::fmt
)

# Link with async IO
target_link_libraries(XER_Lunca PUBLIC XER_AsyncIO)

# C++ standard
target_compile_features(XER_Lunca PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_Lunca PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Lunca PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(XER_Lunca PRIVATE ws2_32 winmm)
endif()
