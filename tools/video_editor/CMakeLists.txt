# VideoEditor core — headless timeline model/history/store/catalog/
# poster modules (Phase 1B). UI-free; consumed by the future
# video_editor exe (1C/1D) and XER_VideoEditor_Tests.
# CONFIGURE_DEPENDS so adding .cc files does not require a manual
# re-configure under Ninja.
# NOTE: argument order matters — <variable> must precede
# CONFIGURE_DEPENDS (flag-first parses but yields an empty list on
# CMake 4.4.x).
file(GLOB VIDEO_EDITOR_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

add_library(XER_VideoEditor_Core STATIC ${VIDEO_EDITOR_CORE_SOURCES})

target_include_directories(XER_VideoEditor_Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(XER_VideoEditor_Core
    PUBLIC
        XER_Video          # video::Frame / FrameExporter (clip_poster)
        XER_ContentVideo   # XER::Content::Video::ProbeVideo (catalog)
        XER_FFmpeg         # Bridge::FFmpeg::VideoDecoder (clip_poster)
        XER_SQL            # SQLiteDatabase (media_catalog)
        XER_Helper         # Path/env/jsoncpp helpers
        jsoncpp_lib        # project_store (.xerproj)
)

target_compile_features(XER_VideoEditor_Core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_VideoEditor_Core PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_VideoEditor_Core PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
