# XER Audio Subsystem CMake Configuration

# Collect audio sources
file(GLOB_RECURSE AUDIO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND AUDIO_SOURCES ${_xer_apple_mm})
endif()
list(FILTER AUDIO_SOURCES EXCLUDE REGEX "/dsp_algorithms/")

file(GLOB_RECURSE AUDIO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

set(_audio_public_deps
    absl::strings
    absl::synchronization
    unofficial::pthreadpool
    unofficial::fxdiv::fxdiv
    FFTW3::fftw3f
    XER_VST3SDK
    XER_Helper
    XER_AsyncIO
    XER_Cache
    XER_Flux_Core)
if(TARGET XER_FFmpeg)
    list(APPEND _audio_public_deps XER_FFmpeg)
endif()
if(TARGET SDL2::SDL2)
    list(APPEND _audio_public_deps SDL2::SDL2)
endif()

if(XER_YA_DLLS)
    # Branded YA.*.dll modules — the monolith splits by subsystem;
    # XER_AudioCore stays as an INTERFACE bundle so consumers keep
    # linking the old name.
    # Leaf-first partition: AudioChannels owns the low-level utilities
    # (interleave/bit_depth/buffer/channel + file_io_codecs) that the
    # engine itself calls — no upward deps. The engine, effects and
    # plugin hosts are mutually dependent (cable_fx_chain/demo call
    # FX+VST3, FX calls back into DSP utils), so they stay in
    # YA.AudioCore; Mix/EffectsPack/Plugin/Player ship as reserved
    # extension modules.
    set(_ya_audio "${AUDIO_SOURCES}")
    xer_ya_partition(_ya_audio
        "YA.AudioChannels|file_io_codecs/|interleave|bit_depth|fifo|buffer|channel"
        "YA.AudioCore|.")
    xer_ya_module(YA_AudioChannels EXPORT_NAME YA.AudioChannels
        SOURCES ${XER_YA_SRC_YA.AudioChannels}
        DEPS ${_audio_public_deps})
    xer_ya_module(YA_AudioCore EXPORT_NAME YA.AudioCore
        SOURCES ${XER_YA_SRC_YA.AudioCore} ${AUDIO_HEADERS}
        DEPS ${_audio_public_deps} YA_AudioChannels XER_AudioDSP
        # DSP symbols stay reachable through this implib — consumers
        # must not link the static archive again (duplication).
        API_TARGETS XER_AudioDSP)
    foreach(_ya_ext IN ITEMS
            "YA_AudioMix|YA.AudioMix"
            "YA_AudioEffectsPack|YA.AudioEffectsPack"
            "YA_AudioPlugin|YA.AudioPlugin"
            "YA_AudioPlayer|YA.AudioPlayer")
        string(REGEX MATCH "^([^|]+)\\|(.*)$" _m "${_ya_ext}")
        xer_ya_module(${CMAKE_MATCH_1} EXPORT_NAME ${CMAKE_MATCH_2}
            DEPS ${_audio_public_deps} YA_AudioCore YA_AudioChannels)
    endforeach()
    if(WIN32)
        foreach(_ya_a YA_AudioCore YA_AudioChannels YA_AudioMix
                YA_AudioEffectsPack YA_AudioPlugin YA_AudioPlayer)
            target_compile_definitions(${_ya_a}_objs PRIVATE _USE_MATH_DEFINES)
            target_link_libraries(${_ya_a} PRIVATE winmm)
        endforeach()
    endif()
    xer_ya_bundle(XER_AudioCore
        MODULES YA_AudioCore YA_AudioMix YA_AudioChannels
                YA_AudioEffectsPack YA_AudioPlugin YA_AudioPlayer
        DEPS ${_audio_public_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
else()
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
    target_link_libraries(XER_AudioCore PUBLIC ${_audio_public_deps})
endif()

if(XER_YA_DLLS)
    # XER_AudioDSP is linked into YA_AudioCore.dll and exported from it —
    # no separate dep on the bundle (avoids implib/archive duplication).
    if(WIN32)
        target_link_libraries(XER_AudioCore INTERFACE ksguid)
    endif()
    # Prefer the static archive on MinGW so YA.AudioChannels carries
    # no libmp3lame-0.dll runtime dependency.
    # Prefer the static archive on MinGW so YA.AudioChannels carries
    # no libmp3lame-0.dll runtime dependency. Static lame calls into
    # mpg123 internally, so libmpg123.a comes along too.
    unset(XER_LAME_LIBRARY CACHE)
    unset(XER_MPG123_LIBRARY CACHE)
    if(MINGW)
        set(_save_sfx ${CMAKE_FIND_LIBRARY_SUFFIXES})
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
    find_library(XER_LAME_LIBRARY NAMES mp3lame lame)
    find_library(XER_MPG123_LIBRARY NAMES mpg123)
    if(MINGW)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ${_save_sfx})
    endif()
    if(XER_LAME_LIBRARY)
        # Static mpg123 calls shlwapi path helpers internally.
        set(_mp3_libs ${XER_LAME_LIBRARY} ${XER_MPG123_LIBRARY}
                      shlwapi)
        # mp3 encoder lives in file_io_codecs -> YA.AudioChannels.
        target_link_libraries(YA_AudioChannels PRIVATE ${_mp3_libs})
        target_link_libraries(XER_AudioCore INTERFACE ${_mp3_libs})
    endif()
else()
    # WASAPI helpers (virtual audio cable, render sink, capture source)
    # use Media Foundation GUIDs from ksguid.
    if(WIN32)
        target_link_libraries(XER_AudioCore PUBLIC ksguid)
    endif()

    unset(XER_LAME_LIBRARY CACHE)
    unset(XER_MPG123_LIBRARY CACHE)
    if(MINGW)
        set(_save_sfx ${CMAKE_FIND_LIBRARY_SUFFIXES})
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
    find_library(XER_LAME_LIBRARY NAMES mp3lame lame)
    find_library(XER_MPG123_LIBRARY NAMES mpg123)
    if(MINGW)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ${_save_sfx})
    endif()
    if(XER_LAME_LIBRARY)
        target_link_libraries(XER_AudioCore PUBLIC
            ${XER_LAME_LIBRARY} ${XER_MPG123_LIBRARY} shlwapi)
    endif()

    target_compile_features(XER_AudioCore PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(XER_AudioCore PRIVATE /W4 /WX- /O2 /arch:AVX2)
    else()
        target_compile_options(XER_AudioCore PRIVATE -Wall -Wextra -Wpedantic -O3 -march=native -msse2 -mavx2)
    endif()
    if(WIN32)
        target_link_libraries(XER_AudioCore PRIVATE winmm)
        target_compile_definitions(XER_AudioCore PRIVATE _USE_MATH_DEFINES)
    endif()
endif()

# Add DSP algorithms subdirectory
add_subdirectory(dsp_algorithms)
