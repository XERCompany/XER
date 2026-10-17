# ============================================================
# EngineQt6Bridge static library
# ============================================================

if(NOT ENGINE_QT6_PROJECT_CC)
    return()
endif()

add_library(EngineQt6Bridge STATIC ${ENGINE_QT6_PROJECT_CC})
target_sources(EngineQt6Bridge PRIVATE ${ENGINE_QT6_PROJECT_H})
target_include_directories(EngineQt6Bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LibNode_INCLUDE_DIR})

target_compile_definitions(EngineQt6Bridge PRIVATE
    ENGINE_HAS_QT6=${ENGINE_HAS_QT6}
    ENGINE_HAS_QT6_CORE=${ENGINE_HAS_QT6_CORE}
    ENGINE_HAS_QT6_GUI=${ENGINE_HAS_QT6_GUI}
    ENGINE_HAS_QT6_WIDGETS=${ENGINE_HAS_QT6_WIDGETS}
    ENGINE_HAS_QT6_WEBCHANNEL=${ENGINE_HAS_QT6_WEBCHANNEL}
    ENGINE_HAS_QT6_WEBVIEW=${ENGINE_HAS_QT6_WEBVIEW}
    ENGINE_HAS_QT6_WEBENGINE=${ENGINE_HAS_QT6_WEBENGINE}
    ENGINE_HAS_QT6_QML=${ENGINE_HAS_QT6_QML}
    ENGINE_HAS_QT6_QUICK=${ENGINE_HAS_QT6_QUICK})

foreach(_qt6_lib Core Gui Xml Pdf Network
        WebEngineCore WebEngineWidgets WebChannel WebView
        Qml Quick Widgets)
    if(Qt6${_qt6_lib}_FOUND)
        target_link_libraries(EngineQt6Bridge PRIVATE Qt6::${_qt6_lib})
    endif()
endforeach()
