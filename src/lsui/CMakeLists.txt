# LSUI — Lua-scripted UI host (lsui.exe)
add_executable(lsui
    main.cc
    lsui.cc
    lsui.h
    lsui_term.cc
)

target_include_directories(lsui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(lsui PUBLIC cxx_std_23)

target_link_libraries(lsui
    PRIVATE
        XER_UI_Core
        XER_Flux_Core
        XER_Lua
        user32
        gdi32
)

if(MSVC)
    target_compile_options(lsui PRIVATE /W3 /EHsc)
endif()

set_target_properties(lsui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
