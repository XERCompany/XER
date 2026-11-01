add_executable(editor main.cc)

target_link_libraries(editor PRIVATE
    absl::strings
    absl::synchronization
    fmt::fmt
    jsoncpp_lib
    XER_AsyncIO
    XER_AudioCore
    XER_Network
    XER_UI_Core
    XER_Flux
)

# Ensure the link step resolves against the libraries built by src/
if(TARGET XER_AsyncIO)
    target_link_libraries(editor PRIVATE XER_AsyncIO)
endif()
if(TARGET XER_AudioCore)
    target_link_libraries(editor PRIVATE XER_AudioCore)
endif()
if(TARGET XER_Network)
    target_link_libraries(editor PRIVATE XER_Network)
endif()
if(TARGET XER_UI_Core)
    target_link_libraries(editor PRIVATE XER_UI_Core)
endif()
if(TARGET XER_Flux)
    target_link_libraries(editor PRIVATE XER_Flux)
endif()

# Optional: make the editor build on Windows without missing runtime paths
set_target_properties(editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
