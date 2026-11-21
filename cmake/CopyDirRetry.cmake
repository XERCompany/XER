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

# Persistent failure is usually a running exe from the bin dir holding
# CEF DLLs / link outputs open. Best-effort: list the known lockers
# whose image path is under DST so the error message names the culprit.
# Everything here is diagnostics — it must never change the outcome
# (the build still fails with FATAL_ERROR, just with a better message).
set(_lock_hint "")
if(WIN32)
    # Trailing separator so a sibling dir ('<dst>_old\...') can't
    # prefix-match — only processes rooted inside DST count. Strip
    # any trailing separators first so we don't emit 'dir\\'.
    string(REGEX REPLACE "[/\\]+$" "" _dst "${DST}")
    file(TO_NATIVE_PATH "${_dst}/" _dst_native)
    string(REPLACE "'" "''" _dst_ps "${_dst_native}")
    # Name-filter via -in: `Get-Process -Name a,b,c` writes a
    # non-terminating error per missing name, which makes powershell
    # -Command exit 1 even when it found matches.
    set(_ps "$names='video_editor','xer_engine_ui_render','xer_browser_shell','XBrowser','XERVacUI','Editor','XER_VST3_CEF'; Get-Process | Where-Object { $names -contains $_.ProcessName } | Where-Object { try { $_.Path.StartsWith('${_dst_ps}', 'OrdinalIgnoreCase') } catch { $false } } | ForEach-Object { \"$($_.ProcessName).exe (PID $($_.Id))\" }")
    execute_process(
        COMMAND powershell -NoProfile -NonInteractive -Command "${_ps}"
        RESULT_VARIABLE _ps_rc
        OUTPUT_VARIABLE _suspects
        ERROR_QUIET
        TIMEOUT 15)
    # Deliberately keyed on output, not _ps_rc — best effort either way.
    if(_suspects)
        string(STRIP "${_suspects}" _suspects)
        string(REPLACE "\n" "; " _suspects "${_suspects}")
        set(_lock_hint " - likely locked by: ${_suspects}; run Build.ps1 (kills locks) or Stop-Process manually")
    endif()
endif()
message(FATAL_ERROR "copy_directory ${SRC} -> ${DST} failed after retries${_lock_hint}")
