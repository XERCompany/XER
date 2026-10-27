# XER Audio DSP Algorithms CMake Configuration

# Collect DSP sources
file(GLOB_RECURSE DSP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE DSP_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create DSP library
add_library(XER_AudioDSP STATIC ${DSP_SOURCES} ${DSP_HEADERS})

target_include_directories(XER_AudioDSP
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Link with vcpkg libraries
target_link_libraries(XER_AudioDSP
    PUBLIC
        absl::strings
        unofficial::pthreadpool
        unofficial::fxdiv::fxdiv
)

# C++ standard
target_compile_features(XER_AudioDSP PUBLIC cxx_std_23)

# Compiler flags with SIMD optimization
if(MSVC)
    target_compile_options(XER_AudioDSP PRIVATE /W4 /WX- /O2 /arch:AVX2)
else()
    target_compile_options(XER_AudioDSP PRIVATE -Wall -Wextra -Wpedantic -O3 -march=native -msse2 -mavx2)
endif()
