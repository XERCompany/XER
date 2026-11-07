# XER Hex Subsystem CMake Configuration
# Header-only byte<->hex conversion + hexdump utilities (content/hex/hex.h).

add_library(XER_Hex INTERFACE)

target_include_directories(XER_Hex
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
