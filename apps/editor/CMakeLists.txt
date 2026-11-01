
add_executable(
    editor
    main.cc
)


target_link_libraries(
    editor PUBLIC
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