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
