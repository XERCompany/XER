# XER OpenCV Engine Bridge — availability probe over OpenCV.
# The library always builds; ENGINE_HAS_OPENCV_BRIDGE (and the OpenCV link)
# only activate when the vcpkg opencv port (or system OpenCV) is found.

add_library(XER_BridgeOpenCV STATIC
    opencv_engine_bridge.h
    opencv_engine_bridge.cc
)

target_include_directories(XER_BridgeOpenCV
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_BridgeOpenCV PUBLIC XER_Helper)

find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    target_include_directories(XER_BridgeOpenCV PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(XER_BridgeOpenCV PUBLIC ${OpenCV_LIBS})
    target_compile_definitions(XER_BridgeOpenCV PUBLIC ENGINE_HAS_OPENCV_BRIDGE=1)
    # MSYS2 ships shared-only OpenCV (libopencv_*-N.dll.a). Resolve the
    # runtime DLL names here (subdirectory scope) so the root list can
    # add -Wl,--delayload for every exe — binaries start without
    # OpenCV on PATH and load it on first cv:: call.
    if(MINGW)
        xer_collect_delayload_dlls(_ocv_dlls ${OpenCV_LIBS})
        set(XER_OPENCV_DELAYLOAD_DLLS "${_ocv_dlls}" CACHE INTERNAL
            "OpenCV DLLs delay-loaded by all executables")
    endif()
else()
    message(STATUS "OpenCV not found — XER_BridgeOpenCV reports unavailable")
endif()

target_compile_features(XER_BridgeOpenCV PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_BridgeOpenCV PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_BridgeOpenCV PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
