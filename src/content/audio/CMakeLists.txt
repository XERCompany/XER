# Content Audio Player CMake Configuration

# Collect audio player sources
file(GLOB AUDIO_PLAYER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND AUDIO_PLAYER_SOURCES ${_xer_apple_mm})
endif()

file(GLOB AUDIO_PLAYER_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create audio player library
add_library(XER_ContentAudio STATIC
    ${AUDIO_PLAYER_SOURCES}
    ${AUDIO_PLAYER_HEADERS}
)

target_include_directories(XER_ContentAudio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Link with audio core library (for AudioSourceLoader)
target_link_libraries(XER_ContentAudio
    PUBLIC
        XER_AudioCore
)

# C++ standard
target_compile_features(XER_ContentAudio PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_ContentAudio PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentAudio PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(XER_ContentAudio PRIVATE ole32)
endif()
