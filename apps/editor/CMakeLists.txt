add_executable(editor main.cc)

target_link_libraries(editor PRIVATE
    absl::strings
    absl::synchronization
    fmt::fmt
    jsoncpp_lib
    XER_AsyncIO
    XER_AudioCore
    XER_AudioDSP
    XER_Network
    XER_UI_Core
)
  
# Optional: make the editor build on Windows without missing runtime paths
set_target_properties(editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
