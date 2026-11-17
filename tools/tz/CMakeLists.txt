# tools/tz — command-line TZ packer and integrity verifier.
#
#   ztzmake   <dir> -o <out.tz|.xexrj>  — pack a directory into TZ
#   ztzvertfy <archive> [-r member] [-l] — verify crc + OVERFLOW

add_executable(ztzmake ztzmake.cc)
target_link_libraries(ztzmake PRIVATE XER_Tz)
set_target_properties(ztzmake PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(ztzvertfy ztzvertfy.cc)
target_link_libraries(ztzvertfy PRIVATE XER_Tz)
set_target_properties(ztzvertfy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
