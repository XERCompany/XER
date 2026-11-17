# Editor effect/audio plugins — self-contained DLLs deployed to
# <bin>/plugins/ and runtime-probed by plugin_host.cc. They link
# nothing but the CRT: the ABI is the header-only sdk/xer_plugin.h.
set(XER_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/plugins")

set(XER_PLUGINS
    xer_fx_adjust
    xer_fx_color
    xer_fx_blur
    xer_fx_style
    xer_fx_distort
    xer_fx_overlay
    xer_audio_fx
)

foreach(plugin ${XER_PLUGINS})
    file(GLOB plugin_sources CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/${plugin}/*.cc")
    add_library(${plugin} MODULE ${plugin_sources})
    target_include_directories(${plugin} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    # Per-config output dirs: the top level seeds
    # CMAKE_<kind>_OUTPUT_DIRECTORY_<CONFIG> to bin/lib, and those
    # per-config target properties beat the generic ones on single- and
    # multi-config generators alike. On Windows a MODULE .dll is a
    # LIBRARY artifact, so both kinds get the plugins dir.
    set_target_properties(${plugin} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${XER_PLUGIN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${XER_PLUGIN_OUTPUT_DIR}")
    foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(${plugin} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${config_upper}
                "${XER_PLUGIN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_${config_upper}
                "${XER_PLUGIN_OUTPUT_DIR}")
    endforeach()
    target_compile_features(${plugin} PRIVATE cxx_std_23)
endforeach()

# Building video_editor keeps plugins current — they're deployed, not
# imported, so no link dependency exists; the ordering dependency
# guarantees a fresh bin/plugins on every build.
add_custom_target(video_editor_plugins DEPENDS ${XER_PLUGINS})
