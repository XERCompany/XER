cmake_minimum_required(VERSION 3.21)

file(MAKE_DIRECTORY "${XBUILD_TEST_DIR}/path with spaces")

function(expect_result expected_code expected_text)
    execute_process(
        COMMAND "${XBUILD_EXECUTABLE}" ${ARGN}
        WORKING_DIRECTORY "${XBUILD_TEST_DIR}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 10)
    if(NOT "${code}" STREQUAL "${expected_code}")
        message(FATAL_ERROR "${ARGN}: expected exit ${expected_code}, got ${code}\n${output}${error}")
    endif()
    string(FIND "${output}${error}" "${expected_text}" found)
    if(found LESS 0)
        message(FATAL_ERROR "${ARGN}: missing '${expected_text}' in output:\n${output}${error}")
    endif()
endfunction()

function(expect_invalid name json path)
    file(WRITE "${XBUILD_TEST_DIR}/${name}.json" "${json}")
    expect_result(1 "${path}" validate "${name}.json")
endfunction()

expect_result(0 "validate" --help)
expect_result(0 "xbuild 0.1.0" --version)
expect_result(0 "clang version" --version)
expect_result(0 "LLVM:" --version)
expect_result(0 "Target:" --version)
expect_result(2 "Usage:")
expect_result(2 "Unknown command" unknown-command)
expect_result(2 "Usage:" validate a.json b.json)
expect_result(2 "Usage:" validate --unknown)
expect_result(1 "Cannot open" validate does-not-exist.json)
expect_result(0 "Configuration valid" validate "${XBUILD_EXAMPLE}")

file(WRITE "${XBUILD_TEST_DIR}/project.json" [=[
{
    // Hooks and expressions must remain inert during validation.
    "targets": [{
        "name": "hello",
        "sources": [{"input": "$<GET_ENV:UNSET_XBUILD_TEST>/main.cc",
                     "start_post": "this-command-must-never-run"}]
    }]
}
]=])
expect_result(0 "Configuration valid" validate)
file(COPY "${XBUILD_TEST_DIR}/project.json"
    DESTINATION "${XBUILD_TEST_DIR}/path with spaces")
expect_result(0 "Configuration valid" validate "path with spaces/project.json")
expect_result(1 "project configuration" validate "path with spaces")

# Force multiple libuv reads; parsing a truncated first chunk must not succeed.
string(REPEAT " " 70000 padding)
file(WRITE "${XBUILD_TEST_DIR}/large.json"
    "${padding}{\"targets\":[{\"name\":\"large\",\"sources\":[{\"input\":\"a.cc\"}]}]}")
expect_result(0 "Configuration valid" validate large.json)

expect_invalid(root "[]" "$")
expect_invalid(missing_targets "{}" "$.targets")
expect_invalid(empty_targets "{\"targets\":[]}" "$.targets")
expect_invalid(wrong_bool "{\"debug\":\"true\",\"targets\":[]}" "$.debug")
expect_invalid(wrong_array "{\"includes\":[42],\"targets\":[]}" "$.includes[0]")
expect_invalid(unknown_field "{\"typo\":true,\"targets\":[]}" "$.typo")
expect_invalid(bad_jobs "{\"depot_tools_sync_jobs\":0,\"targets\":[]}" "$.depot_tools_sync_jobs")
expect_invalid(bad_name "{\"targets\":[{\"name\":\" \",\"sources\":[{\"input\":\"a.cc\"}]}]}" "$.targets[0].name")
expect_invalid(no_sources "{\"targets\":[{\"name\":\"a\"}]}" "$.targets[0].sources")
expect_invalid(duplicate_targets [=[
{"targets":[{"name":"a","sources":[{"input":"a.cc"}]},
            {"name":"a","sources":[{"input":"b.cc"}]}]}
]=] "$.targets[1].name")
expect_invalid(bad_tool [=[
{"targets":[{"name":"a","sources":[{"input":"a.cc","tool":"gcc"}]}]}
]=] "$.targets[0].sources[0].tool")
expect_invalid(empty_source [=[
{"targets":[{"name":"a","sources":[{}]}]}
]=] "$.targets[0].sources[0]")
expect_invalid(bad_sleep [=[
{"targets":[{"name":"a","sources":[{"async_sleep":-1}]}]}
]=] "$.targets[0].sources[0].async_sleep")
expect_invalid(mixed_source [=[
{"targets":[{"name":"a","sources":[{"input":"a.cc","async_sleep":0}]}]}
]=] "$.targets[0].sources[0]")
expect_invalid(missing_glob [=[
{"targets":[{"name":"a","sources":[{"glob_aware_worker":true}]}]}
]=] "$.targets[0].sources[0].glob_input")
expect_invalid(bad_scope [=[
{"scopes":[{"custom_scope":"COPY","system":[{"if":{"IN":4,"TRUE":"ok"}}]}],
 "targets":[{"name":"a","sources":[{"input":"a.cc"}]}]}
]=] "$.scopes[0].system[0].if.IN")
expect_invalid(duplicate_key "{\"targets\":[],\"targets\":[]}" "Invalid JSON")
expect_invalid(trailing_data "{\"targets\":[]} {}" "Invalid JSON")
expect_invalid(trailing_comma "{\"targets\":[],}" "Invalid JSON")
expect_invalid(broken_json "{\"targets\":" "Invalid JSON")
string(REPEAT "[" 1100 nested_open)
string(REPEAT "]" 1100 nested_close)
expect_invalid(excessive_nesting "${nested_open}0${nested_close}" "Invalid JSON")

set(real "${XBUILD_TEST_DIR}/real build")
file(MAKE_DIRECTORY "${real}/src/lib" "${real}/src/app" "${real}/include")
file(WRITE "${real}/include/answer.h" "#pragma once\nconstexpr int base_value = 40;\nint answer();\n")
file(WRITE "${real}/src/lib/answer.cc" "#include \"answer.h\"\nint answer() { return base_value; }\n")
file(WRITE "${real}/src/app/main.cc" "#include \"answer.h\"\n#ifndef FEATURE_ON\n#error missing feature\n#endif\nint main() { return answer() + OFFSET == 42 ? 0 : 1; }\n")
if(WIN32)
    set(compiler msvc)
    set(binary "${real}/program.exe")
else()
    set(compiler clang)
    set(binary "${real}/program")
endif()
file(WRITE "${real}/project.json" "{
  \"tool\": \"${compiler}\", \"jobs\": 2, \"debug\": true,
  \"includes\": [\"include\"], \"features_flags\": [\"FEATURE_ON\"],
  \"scopes\": [{\"custom_scope\":\"offset\",\"system\":[{\"if\":{
    \"IN\":\"$<GET_ENV:XBUILD_SCOPE_TEST>==\\\"1\\\"\",
    \"TRUE\":\"<LOCAL_SET_ENV:OFFSET=2>\", \"ELSE\":\"<LOCAL_SET_ENV:OFFSET=99>\"}}]}],
  \"targets\": [
    {\"name\":\"answer\", \"type\":\"static_library\", \"sources\":[{\"input\":\"<GLOB_RECURSE:src/lib>\"}]},
    {\"name\":\"program\", \"depends_on\":[\"answer\"], \"output\":\"${binary}\",
     \"define_target\":[\"OFFSET=$<GET_ENV:OFFSET>\"], \"sources\":[
       {\"input\":\"<HEADER:include/answer.h>\", \"header\":true},
       {\"async_sleep\":1}, {\"input\":\"src/app/main.cc\"}]}]
}")
set(ENV{XBUILD_SCOPE_TEST} 1)
expect_result(0 "\"actions\"" plan "${real}/project.json")
if(EXISTS "${real}/.xbuild" OR EXISTS "${binary}")
    message(FATAL_ERROR "Planning must not create build artifacts.")
endif()
function(run_build expected_text)
    execute_process(COMMAND "${XBUILD_EXECUTABLE}" build "${real}/project.json"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 180)
    if(NOT code EQUAL 0 OR NOT "${output}${error}" MATCHES "${expected_text}")
        message(FATAL_ERROR "Build failed (${code}): ${output}${error}")
    endif()
endfunction()
run_build("Build succeeded")
execute_process(COMMAND "${binary}" RESULT_VARIABLE code)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "The compiled project returned ${code}.")
endif()
run_build("4 cached")
file(WRITE "${real}/include/answer.h" "#pragma once\nconstexpr int base_value = 41;\nint answer();\n")
run_build("Build succeeded")
execute_process(COMMAND "${binary}" RESULT_VARIABLE code)
if(NOT code EQUAL 1)
    message(FATAL_ERROR "A changed header did not rebuild the dependent executable.")
endif()
file(WRITE "${real}/src/app/main.cc" "this is not C++;\n")
execute_process(COMMAND "${XBUILD_EXECUTABLE}" build "${real}/project.json"
    RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 180)
if(code EQUAL 0)
    message(FATAL_ERROR "A compiler failure was reported as success.")
endif()
expect_result(0 "Cleaned" clean "${real}/project.json")
if(EXISTS "${binary}")
    message(FATAL_ERROR "Clean did not remove its recorded executable.")
endif()
