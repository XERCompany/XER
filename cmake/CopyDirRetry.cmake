# Copies SRC into DST like `cmake -E copy_directory`, but retries a few
# times: Windows Defender/SearchIndex briefly locks freshly-written
# files (notably ~200 MB libcef.dll), which aborts a single-shot copy
# with "Permission denied".
#   cmake -D SRC=<dir> -D DST=<dir> -P CopyDirRetry.cmake
foreach(_try RANGE 1 5)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SRC}" "${DST}"
        RESULT_VARIABLE _rc
        OUTPUT_QUIET ERROR_QUIET)
    if(_rc EQUAL 0)
        message(STATUS "Copied ${SRC} -> ${DST}")
        return()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
endforeach()
message(FATAL_ERROR "copy_directory ${SRC} -> ${DST} failed after retries")
