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
