# xbuild

`xbuild` is a separate C++23 command-line utility in XER. This initial scaffold
loads and validates `project.json`. It does not compile, link, download
dependencies, execute hooks, evaluate scopes or expand expressions.

## Build

The required vcpkg packages are `llvm[clang]`, `abseil`, `libuv`, `jsoncpp`,
`re2` and `range-v3`. Install missing packages for the existing release triplet:

```sh
vcpkg install "llvm[clang]:x64-windows-release" abseil:x64-windows-release libuv:x64-windows-release jsoncpp:x64-windows-release re2:x64-windows-release range-v3:x64-windows-release
```

From the XER root in an x64 Native Tools prompt:

```sh
cmake --preset xbuild
cmake --build --preset xbuild
ctest --preset xbuild
out/build/xbuild/xbuild/xbuild.exe validate xbuild/project.json
```

The `xbuild` preset enables `XER_BUILD_XBUILD=ON`. Other XER configurations can
enable the same option explicitly; its default is `OFF`.

To build only this subproject with the packages above, configure it directly:

```sh
cmake -S xbuild -B out/build/xbuild-standalone -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-release -DVCPKG_MANIFEST_MODE=OFF
cmake --build out/build/xbuild-standalone
ctest --test-dir out/build/xbuild-standalone --output-on-failure
out/build/xbuild-standalone/xbuild.exe validate xbuild/project.json
```

For Linux/macOS, use the appropriate vcpkg toolchain/triplet; the binary has no
`.exe` suffix. Windows/MSVC is the verified configuration.

## Library integration

All seven requested libraries are used by this scaffold:

| Library | Current use |
| --- | --- |
| Clang | Linked compiler version API (`clangBasic`) |
| LLVM | Command-line parsing, version output and default target triple (`LLVMSupport`, `LLVMTargetParser`) |
| Abseil | Diagnostic strings, duplicate-name sets and file-handle cleanup |
| libuv | File open/read/close with RAII and OS error diagnostics |
| JsonCpp | JSON parsing, document storage and type inspection |
| RE2 | Precompiled non-blank string validation |
| range-v3 | Searching the supported field rules |

CMake discovers the installed packages and links their imported targets.
LLVM include directories, definitions and RTTI compatibility are scoped to
`xbuild`. File reads currently use libuv's synchronous API; an asynchronous
build scheduler, compiler driver, AST/IR processing, code generation, JIT and
expression execution belong to the subsequent build engine implementation.
Only the LLVM/Clang components currently used are linked.

## CLI

```text
xbuild --help
xbuild --version
xbuild validate [project.json]
```

`validate` defaults to `project.json` in the current working directory.
Exit codes: `0` = valid configuration/help/version, `1` = read, parse or
validation failure, `2` = invalid command-line usage. Validation errors include
the file and field path, for example `$.targets[0].sources[0].tool`.
`--version` also prints the linked Clang/LLVM versions and default target triple.

## Configuration

`project.json` contains the supplied format, including its `terimal_*` spelling.
The example fixes the missing closing `>` in the `LOG` expression and the nested
quotes in the Node hooks. Source paths and libraries are illustrative placeholders.
It is a configuration example for future build execution, not the CMake definition
used to compile xbuild itself.

The reader accepts `//` and `/* ... */` comments. Duplicate JSON keys, trailing
commas, trailing documents, unknown fields and incorrect field types are errors.
The only required top-level field is a non-empty `targets` array. The JSON reader
limits recursive parsing depth to 128 and reports parser exceptions as errors.

| Location | Fields and types |
| --- | --- |
| Root: string arrays | `includes`, `defines`, `require`, `link_libs`, `depot_tools_fetch`, `features_flags`, `directory_link_libs`, `libs` |
| Root: booleans | `debug`, `depot_tools_enabled`, `depot_tools_make_environment_data`, `terimal_enable_color`, `terimal_verbose`, `terimal_show_time`, `terimal_show_progress`, `vcpkg_enabled`, `vcpkg_enable_manifest` |
| Root: strings | `depot_tools_output_fetching`, `terimal_prefix`, `terimal_sandbox`, `vcpkg_triplet`, `output` |
| Root: integer | `depot_tools_sync_jobs` (1–2147483647) |
| Root: object arrays | `targets`, `scopes` |
| Target | Required unique `name` and non-empty `sources`; optional string arrays `define_target`, `include_target`, `links_libs_target` |
| Source | `input`, `tool`, `out`, `start_post`, `end_post` strings; `glob_files`, `header`, `glob_aware_worker` booleans; `scope_support`, `glob_input` string arrays; `async_sleep` integer (0–2147483647) |
| Scope | Required unique `custom_scope` string and `system` array |
| System entry | An `if` object with required `IN` and `TRUE` strings and optional `ELSE` string |

Each source entry selects exactly one mode:

- `input`: a non-empty path or expression; optional `tool` is `clang` or `msvc`.
- `async_sleep`: a standalone entry containing only the delay.
- `glob_aware_worker: true`: requires a non-empty `glob_input` array.

String array entries must be non-empty. Names, paths, compiler names, `IN`,
sandbox strings and triplets must also be non-empty. Prefixes, hooks and branch
actions may be empty.

Validation checks structure and these constraints only. Expressions such as
`GET_ENV`, `GLOB_RECURSE`, `REGEX`, `HEADER`, `LOCAL_SET_ENV` and `LOG` are retained
verbatim. Their syntax and meaning, sandbox policy values, file existence,
dependency availability and compiler compatibility are not checked yet.
Enabling `depot_tools_enabled` or `vcpkg_enabled` has no side effects.

## Code layout

- `src/main.cc`: CLI dispatch and exit codes.
- `src/project_config.h/.cc`: JSON loading and structural validation.
- `src/file_io.h/.cc`: libuv file reading and resource ownership.
- `../tests/xbuild_cli_test.cmake`: executable-level CLI and configuration tests.

Future build execution can consume the validated document returned by
`XBuild::LoadProjectConfig`.
