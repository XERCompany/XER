# GenYaDef.cmake — build a module .def from archive symbols.
# Runs in script mode: -DNM_EXECUTABLE, -DDEF_OUT, -DLIBNAME,
# -DARCHIVES (,-separated list; entries may be $-genex-expanded paths).
string(REPLACE "," ";" _archives "${ARCHIVES}")
execute_process(
    COMMAND "${NM_EXECUTABLE}" -g --defined-only ${_archives}
    OUTPUT_VARIABLE _nm
    ERROR_QUIET
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "GenYaDef: nm failed on ${ARCHIVES}")
endif()
string(REGEX MATCHALL "[^\r\n]+" _lines "${_nm}")
# Data symbols (vtables, typeinfo, globals) must be exported with the DATA
# keyword — otherwise the generated import library marks them as code and
# consumers bind `&symbol` to a function thunk instead of the IAT slot
# (broken vptrs / auto-import fixups).
set(_syms)
foreach(_line IN LISTS _lines)
    if(_line MATCHES "^[0-9a-fA-F]+ +([TtWwNnDdBbRrCcGgSsVvUu]) +([A-Za-z_.$][A-Za-z0-9_.$@]*)$")
        set(_type "${CMAKE_MATCH_1}")
        set(_name "${CMAKE_MATCH_2}")
        if(_name MATCHES "^\\.refptr\\.")
            continue()
        endif()
        if(_type MATCHES "^[DdBbRrCcGgSsVv]$")
            list(APPEND _syms "${_name} DATA")
        else()
            list(APPEND _syms "${_name}")
        endif()
    endif()
endforeach()
list(REMOVE_DUPLICATES _syms)
list(LENGTH _syms _n)
if(_n GREATER 65000)
    message(FATAL_ERROR "GenYaDef: ${_n} exports exceed the PE limit (65535) for ${LIBNAME}")
endif()
# The LIBRARY name becomes the DLL basename recorded in every consumer's
# import table — it must carry the real filename (with .dll) or the
# loader cannot resolve it at runtime.
file(WRITE "${DEF_OUT}" "LIBRARY \"${LIBNAME}.dll\"\nEXPORTS\n")
foreach(_s IN LISTS _syms)
    file(APPEND "${DEF_OUT}" "    ${_s}\n")
endforeach()
message(STATUS "${LIBNAME}: ${_n} exports")
