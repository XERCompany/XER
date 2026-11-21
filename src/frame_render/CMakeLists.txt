# XER FrameRender — YA.FrameRender.dll
#
# Frame-presentation seam: video::Frame -> normalized BGRA -> transport
# (RenderStore upload when a live UI render-worker pool provides the
# store, in-process pixel path otherwise). Runtime-selected backends —
# the module itself loads no GPU library (vulkan-1.dll is probed for the
# report only; in-process renderers load it themselves).
#
# video::Frame comes from YA.VideoFrame / XER_Video. The RenderStore
# class lives in YA.UIRenderIPC under the YA build (src/ui partition);
# the module borrows a caller-attached store — no transitive UI link
# beyond the one implib.
set(_fr_deps XER_Helper)

if(XER_YA_DLLS)
    set(_fr_frame_dep "")
    if(TARGET YA_VideoFrame)
        set(_fr_frame_dep YA_VideoFrame)
    elseif(TARGET XER_Video)
        set(_fr_frame_dep XER_Video)
    endif()
    set(_fr_store_dep "")
    if(TARGET YA_UIRenderIPC)
        set(_fr_store_dep YA_UIRenderIPC)
    endif()

    xer_ya_module(YA_FrameRender EXPORT_NAME YA.FrameRender
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/frame_render.cc
        DEPS ${_fr_deps} ${_fr_frame_dep} ${_fr_store_dep})
    # UI headers (rendering/renderer.h, rendering/render_store.h,
    # layout/geometry.h) resolve from the src/ui root — the YA module
    # targets export no interface includes.
    target_include_directories(YA_FrameRender_objs PRIVATE
        "${CMAKE_SOURCE_DIR}/src/ui")
    if(_fr_store_dep)
        target_compile_definitions(YA_FrameRender_objs PRIVATE
            XER_FR_HAS_RENDER_STORE=1)
    endif()
    xer_ya_bundle(XER_FrameRender
        MODULES YA_FrameRender
        DEPS ${_fr_deps} ${_fr_frame_dep} ${_fr_store_dep}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/..
                 ${CMAKE_SOURCE_DIR}/src/ui)
else()
    add_library(XER_FrameRender STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/frame_render.cc)
    target_include_directories(XER_FrameRender
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${CMAKE_SOURCE_DIR}/src/ui)
    target_link_libraries(XER_FrameRender
        PUBLIC ${_fr_deps} XER_Video XER_UI_Core)
    target_compile_definitions(XER_FrameRender PUBLIC
        XER_FR_HAS_RENDER_STORE=1)
    target_compile_features(XER_FrameRender PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(XER_FrameRender PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_FrameRender PRIVATE
            -Wall -Wextra -Wpedantic -O3)
    endif()
endif()
