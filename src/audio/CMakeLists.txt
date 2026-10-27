# XER Audio Subsystem CMake Configuration

# Collect audio sources
file(GLOB_RECURSE AUDIO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
list(FILTER AUDIO_SOURCES EXCLUDE REGEX "/dsp_algorithms/")

file(GLOB_RECURSE AUDIO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create audio core library
add_library(XER_AudioCore STATIC
    ${AUDIO_SOURCES}
    ${AUDIO_HEADERS}
)

target_include_directories(XER_AudioCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link with vcpkg libraries
target_link_libraries(XER_AudioCore
    PUBLIC
        absl::strings
        absl::synchronization
        unofficial::pthreadpool
        unofficial::fxdiv::fxdiv
)

# Link with FFmpeg if available
if(TARGET XER_FFmpeg)
    target_link_libraries(XER_AudioCore PUBLIC XER_FFmpeg)
endif()

# Link with SDL2 if available
if(TARGET SDL2::SDL2)
    target_link_libraries(XER_AudioCore PUBLIC SDL2::SDL2)
endif()

# C++ standard
target_compile_features(XER_AudioCore PUBLIC cxx_std_23)

# Compiler flags with SIMD optimization
if(MSVC)
    target_compile_options(XER_AudioCore PRIVATE /W4 /WX- /O2 /arch:AVX2)
else()
    target_compile_options(XER_AudioCore PRIVATE -Wall -Wextra -Wpedantic -O3 -march=native -msse2 -mavx2)
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(XER_AudioCore PRIVATE winmm)
endif()

# Add DSP algorithms subdirectory
add_subdirectory(dsp_algorithms)
target_link_libraries(XER_AudioCore PUBLIC XER_AudioDSP)
