  
file(GLOB_RECURSE VST3_CEF_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/vst3_cef.cc"
)                

file(GLOB_RECURSE VST3_CEF_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/vst3_cef.h"
)
 
add_executable(XER_VST3_CEF ${VST3_CEF_SOURCES} ${VST3_CEF_HEADERS})

target_include_directories(XER_VST3_CEF
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
 
find_package(jsoncpp CONFIG REQUIRED)
find_package(V8 QUIET)
find_package(yaml-cpp QUIET)
find_package(OpenColorIO QUIET)
find_package(OpenImageIO QUIET)
find_package(librsvg QUIET)
find_package(CEF QUIET)
if(TARGET CEF::libcef)
    add_custom_target(XER_VST3_CEF
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CEF_RELEASE_DIR}"
                "$<TARGET_FILE_DIR:XER_VST3_CEF>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CEF_RESOURCES_DIR}"
                "$<TARGET_FILE_DIR:XER_VST3_CEF>"
        COMMENT "Deploying CEF runtime (libcef.dll, *.pak, locales)"
        VERBATIM)
    add_dependencies(XER_VST3_CEF XER_VST3_CEF)
    message(STATUS "CEF deploy target enabled (${CEF_ROOT})")
endif()
target_link_libraries(XER_VST3_CEF
    PUBLIC
        absl::synchronization
        absl::flat_hash_map
        absl::flat_hash_set
        jsoncpp_lib
        XER_Hex
        XER_Video          # video::Frame / FrameExporter (clip_poster)
        XER_ContentVideo   # XER::Content::Video::ProbeVideo (catalog)
        XER_FFmpeg         # Bridge::FFmpeg::VideoDecoder (clip_poster)
        XER_SQL            # SQLiteDatabase (media_catalog)
        XER_Cache          # atomic export temp/commit helpers
        XER_NetClient      # app_super_net fetch/download (native_modules)
        XER_Tz             # .xexrj archives (extensions/ext_host)
        XER_Cef            # xer-ext:// scheme (stub when CEF absent)
        XER_Helper         # Path/env/jsoncpp helpers
        jsoncpp_lib        # project_store (.xerproj)
        XER_AudioCore      # VST3 host (plugin_wrappers) + DSP
) 
 
if(TARGET libuv::uv_a OR TARGET libuv::uv)
    target_link_libraries(XER_VST3_CEF PUBLIC $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
endif()
if(TARGET librsvg::librsvg)
    target_link_libraries(XER_VST3_CEF PUBLIC librsvg::librsvg)
    target_compile_definitions(XER_VST3_CEF PUBLIC
        XER_VE_HAS_RSVG=1)
endif()
if(TARGET OpenColorIO::OpenColorIO)
    target_link_libraries(XER_VST3_CEF PUBLIC
        OpenColorIO::OpenColorIO)
    target_compile_definitions(XER_VST3_CEF PUBLIC
        XER_VE_HAS_OCIO=1)
endif()
if(TARGET OpenImageIO::OpenImageIO)
    target_link_libraries(XER_VST3_CEF PUBLIC
        OpenImageIO::OpenImageIO)
    target_compile_definitions(XER_VST3_CEF PUBLIC
        XER_VE_HAS_OIIO=1)
endif()
if(TARGET V8::V8)
    target_link_libraries(XER_VST3_CEF PUBLIC V8::V8)
    target_compile_definitions(XER_VST3_CEF PUBLIC
        XER_VE_HAS_V8=1)
endif()
if(TARGET yaml-cpp::yaml-cpp)
    target_link_libraries(XER_VST3_CEF PUBLIC yaml-cpp::yaml-cpp)
    target_compile_definitions(XER_VST3_CEF PUBLIC
        XER_VE_HAS_YAMLCPP=1)
elseif(TARGET yaml-cpp)
    target_link_libraries(XER_VST3_CEF PUBLIC yaml-cpp)
    target_compile_definitions(XER_VST3_CEF PUBLIC
        XER_VE_HAS_YAMLCPP=1)
endif()

target_compile_features(XER_VST3_CEF PUBLIC cxx_std_23)
 
if(MSVC)
    target_compile_options(XER_VST3_CEF PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_VST3_CEF PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
 
if(WIN32) 
    target_link_libraries(XER_VST3_CEF PUBLIC ws2_32 iphlpapi ole32 wbemuuid)
elseif(APPLE)
    # macOS: no extra libs required for the providers in this module.
else()
    # Linux: nothing extra required (/proc, /sys, getifaddrs are libc).
endif()
