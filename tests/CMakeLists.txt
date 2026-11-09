add_executable(XER_UI_Tests
    ui/test_main.cc
    ui/test_harness.h
    ui/theme_widget_tree_tests.cc
    ui/native_window_tests.cc
    ui/flux_backend_tests.cc
    ui/interactive_widget_tests.cc
    ui/presentation_widget_tests.cc
    ui/ffmpeg_avatar_tests.cc
    ui/ffmpeg_video_decode_tests.cc
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
    XER_ContentDawn
    XER_ContentSim
    XER_ContentNetTest
    XER_Network
    XER_Helper
)
target_include_directories(XER_ContentApps_Smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(XER_ContentApps_Smoke PRIVATE cxx_std_23)
add_test(NAME XER_ContentApps_Smoke COMMAND XER_ContentApps_Smoke)

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
