# YaDll.cmake — wrap static .a archives into shared YA.*.dll binaries.
#
# Embedding the full MSYS2 static closure (FFmpeg + ~150 transitive
# archives, assimp, ...) into every executable produced ~170 MB exes.
# xer_add_ya_dll() instead links those archives once into a shared DLL
# named YA.<name>.dll; executables then link its import library — plain
# implicit dynamic linking, no LoadLibrary.
#
#   xer_add_ya_dll(<target>
#       EXPORT_NAME "YA.MediaDecoderUniversal"
#       API_ARCHIVES <a>...   # archives existing at configure time
#       API_TARGETS  <t>...   # static lib targets — their archives get
#                              #   whole-archived too (def generated at
#                              #   build time from $<TARGET_FILE>)
#       ARCHIVES     <a>...   # hidden deps (--start-group), not exported
#       LIBS         <items>  # leftover -l/-L/YA-target link items)
#
# The public surface is a generated .def: PE caps exports at 65535 and
# Rust archives alone carry ~90k internals, so only API inputs' global
# symbols are exported; ARCHIVES are resolved inside the DLL but stay
# hidden.

function(xer_add_ya_dll target)
    cmake_parse_arguments(XAY "" "EXPORT_NAME"
                          "API_ARCHIVES;API_TARGETS;ARCHIVES;LIBS" ${ARGN})
    if(NOT XAY_EXPORT_NAME)
        set(XAY_EXPORT_NAME "${target}")
    endif()

    set(_ya_dir "${CMAKE_BINARY_DIR}/ya")
    file(MAKE_DIRECTORY "${_ya_dir}")

    # C++ stub so the link uses the clang++ driver (libc++ init for any
    # C++ archives inside, e.g. assimp).
    set(_stub "${_ya_dir}/${target}_stub.cpp")
    if(NOT EXISTS "${_stub}")
        file(WRITE "${_stub}"
             "// Stub TU for ${XAY_EXPORT_NAME} — all code comes from archives.\n")
    endif()

    set(_link_items ${XAY_LIBS} -Wl,--allow-multiple-definition)
    set(_api_inputs ${XAY_API_ARCHIVES})
    foreach(_t IN LISTS XAY_API_TARGETS)
        list(APPEND _api_inputs "$<TARGET_FILE:${_t}>")
    endforeach()
    if(_api_inputs)
        # Module-def file generated at build time: input archives may be
        # produced by this same build (target genexes), so nm runs in a
        # custom command, not at configure.
        set(_def "${_ya_dir}/${target}.def")
        string(JOIN "," _api_joined ${_api_inputs})
        add_custom_command(
            OUTPUT "${_def}"
            COMMAND "${CMAKE_COMMAND}"
                    "-DNM_EXECUTABLE=${CMAKE_NM}"
                    "-DDEF_OUT=${_def}"
                    "-DLIBNAME=${XAY_EXPORT_NAME}"
                    "-DARCHIVES=${_api_joined}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenYaDef.cmake"
            DEPENDS ${_api_inputs} ${XAY_API_TARGETS}
                    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenYaDef.cmake"
            COMMENT "Generating ${XAY_EXPORT_NAME} export table")
        add_custom_target(${target}_def DEPENDS "${_def}")
        list(APPEND _link_items
             -Wl,--whole-archive ${_api_inputs} -Wl,--no-whole-archive
             "${_def}")
    else()
        list(APPEND _link_items -Wl,--export-all-symbols)
    endif()
    if(XAY_ARCHIVES)
        list(APPEND _link_items
             -Wl,--start-group ${XAY_ARCHIVES} -Wl,--end-group)
    endif()

    add_library(${target} SHARED "${_stub}")
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${XAY_EXPORT_NAME}"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    target_link_libraries(${target} PRIVATE ${_link_items})
    if(_api_inputs)
        add_dependencies(${target} ${target}_def)
    endif()

    # Same selective static runtime as executables so the DLL does not
    # depend on MSYS2 libc++.dll/libunwind/libwinpthread either.
    if(MINGW AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_link_options(${target} PRIVATE
            -Wl,-Bstatic,-lc++,-lc++abi,-lunwind,-lwinpthread,-Bdynamic)
    endif()
endfunction()


# xer_ya_module() — build a YA.*.dll directly from project sources.
# The old monolithic XER_* target becomes an INTERFACE bundle over the
# YA modules so consumers need no link-line changes.
function(xer_ya_module target)
    cmake_parse_arguments(XYM "" "EXPORT_NAME"
                          "SOURCES;DEPS;API_TARGETS" ${ARGN})
    if(NOT XYM_EXPORT_NAME)
        set(XYM_EXPORT_NAME "${target}")
    endif()
    if(NOT XYM_SOURCES)
        # Reserved branded module: no sources matched the partition, emit
        # a stub TU so the DLL still exists as a link target.
        set(_stub "${CMAKE_BINARY_DIR}/ya/${target}_stub.cpp")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ya")
        if(NOT EXISTS "${_stub}")
            file(WRITE "${_stub}"
                 "// Reserved YA module ${XYM_EXPORT_NAME} — extension point.\n")
        endif()
        set(XYM_SOURCES "${_stub}")
    endif()

    # Objects first: the export surface must be exactly the module's own
    # code. --export-all-symbols would also re-export every statically
    # linked dep (absl/...) and collide with sibling YA DLLs exporting
    # the same archives ("was replaced" link errors).
    add_library(${target}_objs OBJECT ${XYM_SOURCES})
    target_include_directories(${target}_objs PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
    if(XYM_DEPS)
        # DEPS are needed at compile time too (usage requirements:
        # include dirs, defines) — on an OBJECT lib target_link_libraries
        # only propagates those, no link happens.
        target_link_libraries(${target}_objs PRIVATE ${XYM_DEPS})
    endif()

    # API_TARGETS are static lib targets whose symbols the module also
    # exports (e.g. a dsp archive linked inside must stay reachable by
    # consumers through this DLL's implib). Listed separately from DEPS
    # so only their symbols join the export table.
    set(_def_inputs "$<TARGET_OBJECTS:${target}_objs>")
    set(_def_deps $<TARGET_OBJECTS:${target}_objs>)
    foreach(_t IN LISTS XYM_API_TARGETS)
        list(APPEND _def_inputs "$<TARGET_FILE:${_t}>")
        list(APPEND _def_deps "$<TARGET_FILE:${_t}>")
    endforeach()
    set(_def "${CMAKE_BINARY_DIR}/ya/${target}.def")
    add_custom_command(
        OUTPUT "${_def}"
        COMMAND "${CMAKE_COMMAND}"
                "-DNM_EXECUTABLE=${CMAKE_NM}"
                "-DDEF_OUT=${_def}"
                "-DLIBNAME=${XYM_EXPORT_NAME}"
                "-DARCHIVES=$<JOIN:${_def_inputs},,>"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenYaDef.cmake"
        DEPENDS ${_def_deps} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenYaDef.cmake"
        COMMENT "Generating ${XYM_EXPORT_NAME} export table")
    add_custom_target(${target}_def DEPENDS "${_def}")

    add_library(${target} SHARED $<TARGET_OBJECTS:${target}_objs> "${_def}")
    add_dependencies(${target} ${target}_def)
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${XYM_EXPORT_NAME}"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    target_link_options(${target} PRIVATE -Wl,--allow-multiple-definition)
    if(XYM_DEPS)
        target_link_libraries(${target} PRIVATE ${XYM_DEPS})
    endif()
    if(MINGW AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_link_options(${target} PRIVATE
            -Wl,-Bstatic,-lc++,-lc++abi,-lunwind,-lwinpthread,-Bdynamic)
    endif()
endfunction()


# xer_ya_bundle() — recreate a monolithic XER_* name as an INTERFACE
# target that fans out to the YA modules + its original public deps.
function(xer_ya_bundle xtarget)
    cmake_parse_arguments(XB "" "" "MODULES;DEPS;INCLUDES;DEFINES" ${ARGN})
    add_library(${xtarget} INTERFACE)
    target_link_libraries(${xtarget} INTERFACE ${XB_MODULES} ${XB_DEPS})
    if(XB_INCLUDES)
        target_include_directories(${xtarget} INTERFACE ${XB_INCLUDES})
    endif()
    if(XB_DEFINES)
        target_compile_definitions(${xtarget} INTERFACE ${XB_DEFINES})
    endif()
    target_compile_features(${xtarget} INTERFACE cxx_std_23)
endfunction()


# xer_collect_delayload_dlls(<out-var> <item>...)
# Resolves imported targets / implib paths in ARGN into actual runtime
# DLL filenames (for -Wl,--delayload). Handles:
#   - imported CMake targets (scans IMPORTED_IMPLIB*/LOCATION* and
#     recurses into INTERFACE_LINK_LIBRARIES),
#   - literal implib paths ending in .dll.a (name → name.dll).
function(xer_collect_delayload_dlls out_var)
    set(_dlls)
    foreach(_item IN LISTS ARGN)
        set(_implib "")
        if(TARGET ${_item})
            foreach(_p IMPORTED_IMPLIB IMPORTED_IMPLIB_RELEASE
                        IMPORTED_IMPLIB_NOCONFIG IMPORTED_LOCATION
                        IMPORTED_LOCATION_RELEASE
                        IMPORTED_LOCATION_NOCONFIG)
                get_target_property(_v ${_item} ${_p})
                if(_v)
                    set(_implib "${_v}")
                    break()
                endif()
            endforeach()
            if(NOT _implib)
                get_target_property(_iface ${_item}
                                    INTERFACE_LINK_LIBRARIES)
                if(_iface)
                    xer_collect_delayload_dlls(_sub ${_iface})
                    list(APPEND _dlls ${_sub})
                endif()
                continue()
            endif()
        else()
            set(_implib "${_item}")
        endif()
        if(_implib MATCHES "\\.dll\\.a$")
            get_filename_component(_f "${_implib}" NAME)
            get_filename_component(_dir "${_implib}" DIRECTORY)
            string(REGEX REPLACE "\\.dll\\.a$" "" _stem "${_f}")
            get_filename_component(_bindir "${_dir}/../bin" ABSOLUTE)
            # Runtime DLL name may differ from the implib name
            # (libfoo.dll.a → libfoo-N.dll, versioned). Prefer the
            # versioned match in the sibling bin dir.
            set(_matched FALSE)
            foreach(_cand "${_bindir}/${_stem}.dll" "${_dir}/${_stem}.dll")
                if(EXISTS "${_cand}")
                    get_filename_component(_nm "${_cand}" NAME)
                    list(APPEND _dlls "${_nm}")
                    set(_matched TRUE)
                    break()
                endif()
            endforeach()
            if(NOT _matched)
                # Versioned runtime names: libfoo-N.dll AND
                # libfoo_M_N.dll (OCIO-style underscore suffix).
                file(GLOB _vg "${_bindir}/${_stem}-*.dll"
                              "${_bindir}/${_stem}_*.dll")
                if(_vg)
                    foreach(_v IN LISTS _vg)
                        get_filename_component(_v "${_v}" NAME)
                        list(APPEND _dlls "${_v}")
                    endforeach()
                    set(_matched TRUE)
                endif()
            endif()
            if(NOT _matched)
                list(APPEND _dlls "${_stem}.dll")
            endif()
        elseif(_implib MATCHES "^-l(.+)$" OR
               (NOT _implib MATCHES "[/\\\\.]" AND
                NOT _implib MATCHES "\\.a$"))
            # -lfoo / bare name: resolve against toolchain import libs
            # (libfoo.dll.a → libfoo.dll; also libfoo-N.dll versioned).
            string(REGEX REPLACE "^-l" "" _n "${_implib}")
            set(_found FALSE)
            foreach(_libdir ${CMAKE_PREFIX_PATH}
                            ${CMAKE_LIBRARY_PATH}
                            "${CMAKE_SYSROOT}/clang64/lib"
                            "C:/tools/msys64/clang64/lib")
                if(EXISTS "${_libdir}/lib${_n}.dll.a")
                    list(APPEND _dlls "lib${_n}.dll")
                    set(_found TRUE)
                    break()
                elseif(EXISTS "${_libdir}/${_n}.dll.a")
                    list(APPEND _dlls "${_n}.dll")
                    set(_found TRUE)
                    break()
                endif()
            endforeach()
            # Versioned runtime names (libfoo-500.dll): glob bin dirs
            # next to the candidate libdirs.
            if(NOT _found)
                foreach(_libdir ${CMAKE_PREFIX_PATH}
                                ${CMAKE_LIBRARY_PATH}
                                "${CMAKE_SYSROOT}/clang64/lib"
                                "C:/tools/msys64/clang64/lib")
                    get_filename_component(_bindir
                        "${_libdir}/../bin" ABSOLUTE)
                    file(GLOB _vg "${_bindir}/lib${_n}-*.dll"
                                  "${_bindir}/${_n}-*.dll")
                    foreach(_v IN LISTS _vg)
                        get_filename_component(_v "${_v}" NAME)
                        list(APPEND _dlls "${_v}")
                        set(_found TRUE)
                    endforeach()
                    if(_found)
                        break()
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _dlls)
    set(${out_var} "${_dlls}" PARENT_SCOPE)
endfunction()

# xer_stage_runtime_dlls(<out_dir> <toolchain_bin> <seed-dll>...)
# Recursively walks the PE import + delay-import tables of each seed
# DLL (via CMAKE_OBJDUMP -p) and copies every dependency that resolves
# inside <toolchain_bin> into <out_dir>. Names found in neither dir are
# OS-provided (KERNEL32/…, api-ms-*, …) and skipped; names already in
# <out_dir> are followed but not re-copied. Runs at configure time so
# every consumer target inherits the staged runtime for free — the
# delay-load helper only defers a missing DLL to first call, where it
# still hard-kills the process (exit 127 under MSYS2).
function(xer_stage_runtime_dlls out_dir toolchain_bin)
    file(MAKE_DIRECTORY "${out_dir}")
    set(_queue ${ARGN})
    set(_seen)
    set(_staged)
    while(_queue)
        list(POP_FRONT _queue _dll)
        string(TOLOWER "${_dll}" _lc)
        if(_lc IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen "${_lc}")
        if(EXISTS "${out_dir}/${_dll}")
            set(_src "${out_dir}/${_dll}")
        elseif(EXISTS "${toolchain_bin}/${_dll}")
            set(_src "${toolchain_bin}/${_dll}")
        else()
            continue()  # OS-provided / non-toolchain — nothing to stage
        endif()
        execute_process(
            COMMAND "${CMAKE_OBJDUMP}" -p "${_src}"
            OUTPUT_VARIABLE _imports ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCHALL "DLL Name:[ \t]*[^ \t\r\n]+" _deps "${_imports}")
        foreach(_line IN LISTS _deps)
            string(REGEX REPLACE "^DLL Name:[ \t]*" "" _dep "${_line}")
            string(STRIP "${_dep}" _dep)
            string(TOLOWER "${_dep}" _dep_lc)
            if(NOT _dep_lc IN_LIST _seen)
                list(APPEND _queue "${_dep}")
            endif()
        endforeach()
        if(_src STREQUAL "${toolchain_bin}/${_dll}")
            file(COPY "${_src}" DESTINATION "${out_dir}")
            list(APPEND _staged "${_dll}")
        endif()
    endwhile()
    list(LENGTH _staged _n)
    message(STATUS "xer_stage_runtime_dlls: ${_n} toolchain DLL(s) "
                   "staged into ${out_dir} (seeds: ${ARGN})")
endfunction()

# xer_delayload(<target> <dll>...)
# Adds --delayload link options: the binary starts without those DLLs
# on PATH; they load lazily on first call into them.
function(xer_delayload target)
    set(_opts)
    foreach(_d IN LISTS ARGN)
        list(APPEND _opts "-Wl,--delayload,${_d}")
    endforeach()
    if(_opts)
        get_target_property(_ttype ${target} TYPE)
        if(_ttype STREQUAL "INTERFACE_LIBRARY")
            target_link_options(${target} INTERFACE ${_opts})
        else()
            target_link_options(${target} PRIVATE ${_opts})
        endif()
    endif()
endfunction()

# Split a file list into named groups by regex, first match wins.
#   xer_ya_partition(<srcs-var>
#       "YA.AudioCore|audio_core/" "YA.AudioMix|mixer" ...)
# Sets XER_YA_SRC_<name> in parent scope; removes matched files from
# <srcs-var> so the leftover goes to the last/catch-all group.
function(xer_ya_partition srcs_var)
    foreach(_spec IN LISTS ARGN)
        string(REGEX MATCH "^([^|]+)\\|(.*)$" _m "${_spec}")
        set(_name "${CMAKE_MATCH_1}")
        set(_re "${CMAKE_MATCH_2}")
        set(_group)
        foreach(_f IN LISTS ${srcs_var})
            if(_f MATCHES "${_re}")
                list(APPEND _group "${_f}")
            endif()
        endforeach()
        if(_group)
            foreach(_f IN LISTS _group)
                list(REMOVE_ITEM ${srcs_var} "${_f}")
            endforeach()
            set(${srcs_var} "${${srcs_var}}" PARENT_SCOPE)
        endif()
        set(XER_YA_SRC_${_name} "${_group}" PARENT_SCOPE)
    endforeach()
endfunction()
