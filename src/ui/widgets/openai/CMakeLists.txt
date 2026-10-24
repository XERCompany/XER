# OpenAI UI Widgets CMake Configuration

# Collect OpenAI widget sources
file(GLOB_RECURSE OPENAI_WIDGET_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/openai/*.cc"
)

file(GLOB_RECURSE OPENAI_WIDGET_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/openai/*.h"
)

# Create OpenAI UI widgets library
add_library(XER_UI_OpenAI STATIC ${OPENAI_WIDGET_SOURCES} ${OPENAI_WIDGET_HEADERS})

target_include_directories(XER_UI_OpenAI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../..
        ${CMAKE_CURRENT_SOURCE_DIR}/openai
)

# Link with XER components
target_link_libraries(XER_UI_OpenAI
    PUBLIC
        XER_OpenAI
        XER_UI_Core
        XER_UI_Widgets
        XER_AsyncIO
)

# Link with UI dependencies
if(WIN32)
    target_link_libraries(XER_UI_OpenAI
        PUBLIC
            user32
            gdi32
            d2d1
            dxgi
    )
endif()

# C++ standard
target_compile_features(XER_UI_OpenAI PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_UI_OpenAI PRIVATE /W4 /WX-)
else()
    target_compile_options(XER_UI_OpenAI PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Install
install(TARGETS XER_UI_OpenAI
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${OPENAI_WIDGET_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ui/widgets/openai
)
