add_executable(XER_UI_Tests
    ui/test_main.cc
    ui/test_harness.h
    ui/theme_widget_tree_tests.cc
    ui/native_window_tests.cc
    ui/flux_backend_tests.cc
    ui/interactive_widget_tests.cc
    ui/presentation_widget_tests.cc
    ui/flex_layout_tests.cc
    ui/ffmpeg_avatar_tests.cc
    ui/ffmpeg_video_decode_tests.cc
    ui/ffmpeg_audio_tap_tests.cc
    ui/ffmpeg_av_mux_tests.cc
    ui/style_host_tests.cc
    ui/render_store_tests.cc
    ui/renderer_provider_tests.cc
)

target_link_libraries(XER_UI_Tests
    PRIVATE
        XER_UI_Core
        XER_UI_Flux
        XER_Flux_Core
        XER_Helper
)

target_compile_features(XER_UI_Tests PRIVATE cxx_std_23)

add_test(NAME XER_UI_Tests COMMAND XER_UI_Tests)

add_executable(XER_SecurityFlash_Smoke security_flash_smoke.cc)
target_link_libraries(XER_SecurityFlash_Smoke PRIVATE XER_SecurityFlash)
target_compile_features(XER_SecurityFlash_Smoke PRIVATE cxx_std_23)
add_test(NAME XER_SecurityFlash_Smoke COMMAND XER_SecurityFlash_Smoke)

add_executable(XER_ContentApps_Smoke content_apps_smoke.cc)
target_link_libraries(XER_ContentApps_Smoke PRIVATE
    XER_ContentModel3D
    XER_ContentModelDiff
    XER_ContentFormatConv
    XER_ContentPasswordGen
    XER_ContentSim
    XER_ContentNetTest
    XER_Network
    XER_Helper
)
target_include_directories(XER_ContentApps_Smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(XER_ContentApps_Smoke PRIVATE cxx_std_23)
add_test(NAME XER_ContentApps_Smoke COMMAND XER_ContentApps_Smoke)

add_executable(XER_Media_Smoke media_smoke.cc)
target_link_libraries(XER_Media_Smoke PRIVATE XER_Media XER_Video)
target_include_directories(XER_Media_Smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(XER_Media_Smoke PRIVATE cxx_std_23)
add_test(NAME XER_Media_Smoke COMMAND XER_Media_Smoke)

# Dawn's D3D12 backend loads d3dcompiler_47.dll / dxcompiler.dll /
# dxil.dll through its runtime search paths (module dir, exe dir).
# Ship the Windows SDK copies next to the exe.
if(WIN32)
    foreach(_d3d_dll IN ITEMS d3dcompiler_47.dll dxcompiler.dll dxil.dll)
        file(GLOB _d3dcompiler_candidates
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/${_d3d_dll}")
        if(_d3dcompiler_candidates)
            list(SORT _d3dcompiler_candidates COMPARE NATURAL)
            list(GET _d3dcompiler_candidates -1 _d3dcompiler_dll)
            add_custom_command(TARGET XER_ContentApps_Smoke POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_d3dcompiler_dll}"
                    "$<TARGET_FILE_DIR:XER_ContentApps_Smoke>")
        endif()
    endforeach()
endif()

# XER_Video smoke — first real consumer of the (formerly orphan) video
# pipeline lib. MSVC-gated like the rest of the video path; the test
# itself also skips FFmpeg-backed checks when the bridge is stubbed.
if(MSVC)
    add_executable(XER_Video_Smoke video/xer_video_smoke.cc)
    target_link_libraries(XER_Video_Smoke PRIVATE XER_Video)
    target_compile_features(XER_Video_Smoke PRIVATE cxx_std_23)
    add_test(NAME XER_Video_Smoke COMMAND XER_Video_Smoke)
endif()

# VideoEditor headless core — model/history/store/catalog/poster unit
# tests. Plain-assert style like XER_Video_Smoke; FFmpeg-backed checks
# self-skip via XER::Bridge::FFmpeg::IsAvailable(). Registered
# unconditionally: XER_VideoEditor_Core's FFmpeg deps degrade to stubs
# off-Windows and unused XER_Video objects are never pulled from the
# static lib (if a non-MSVC link ever breaks, gate with if(MSVC) like
# XER_Video_Smoke).
add_executable(XER_VideoEditor_Tests
    video/video_editor_test_util.h
    video/video_editor_test_main.cc
    video/video_editor_model_tests.cc
    video/video_editor_history_tests.cc
    video/video_editor_store_tests.cc
    video/video_editor_catalog_tests.cc
    video/video_editor_poster_tests.cc
    video/video_editor_decoder_tests.cc
    video/video_editor_compositor_tests.cc
    video/video_editor_compositor2_tests.cc
    video/video_editor_mixer_tests.cc
    video/video_editor_export_tests.cc
    video/video_editor_export_preset_tests.cc
    video/video_editor_vst3_tests.cc
    video/video_editor_lab_tests.cc
    video/video_editor_lab2_tests.cc
    video/video_editor_playback_tests.cc
    video/video_editor_ops_tests.cc
    video/video_editor_generated_export_tests.cc
    video/video_editor_model2_tests.cc
    video/video_editor_timecode_tests.cc
    video/video_editor_autosave_tests.cc
    video/video_editor_media_prep_tests.cc
    video/video_editor_source_edit_tests.cc
    video/plugin_host_tests.cc
    video/app_runtime_tests.cc
    video/script_host_tests.cc
    video/net_client_tests.cc
    video/ext_host_tests.cc
)
target_link_libraries(XER_VideoEditor_Tests PRIVATE XER_VideoEditor_Core)
target_compile_features(XER_VideoEditor_Tests PRIVATE cxx_std_23)
# The app_runtime boot tests read deployed bin/assets — keep the copy
# fresh even when only the tests target is built (the copy target is
# defined inside the WIN32 UI block, so guard it).
if(TARGET video_editor_assets)
    add_dependencies(XER_VideoEditor_Tests video_editor_assets)
endif()
add_test(NAME XER_VideoEditor_Tests COMMAND XER_VideoEditor_Tests)
