# ============================================================
# EngineBuilder main executable
# ============================================================

if(ENABLE_V8 AND ENGINE_HAS_LLVM_CLANG)
add_executable(XER ${ENGINE_PROJECT_CC})
target_sources(XER PRIVATE ${ENGINE_PROJECT_H})

target_include_directories(XER PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LIBXML2_INCLUDE_DIRS}
    ${LLVM_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${LLVM_DEFINITIONS})

if(ENGINE_HAS_ZSTD)
    target_include_directories(XER PRIVATE ${ZSTD_INCLUDE_DIR})
endif()
if(ENGINE_HAS_BROTLI)
    target_include_directories(XER PRIVATE ${BROTLI_INCLUDE_DIR})
endif()
if(ENGINE_HAS_FFI)
    target_include_directories(XER PRIVATE ${FFI_INCLUDE_DIR})
endif()
if(ENGINE_HAS_FFTW3F)
    target_include_directories(XER PRIVATE ${FFTW3_INCLUDE_DIR})
endif()
if(ENGINE_HAS_ICU)
    target_include_directories(XER PRIVATE ${ICU_INCLUDE_DIRS})
endif()
if(ENGINE_HAS_HDF5)
    target_include_directories(XER PRIVATE ${HDF5_INCLUDE_DIRS})
endif()

target_compile_definitions(XER PRIVATE
    ${LLVM_DEFINITIONS}
    ENABLE_CUDA=$<BOOL:${ENABLE_CUDA}>
    ENABLE_CUDNN=$<BOOL:${ENABLE_CUDNN}>
    ENABLE_AI=$<BOOL:${ENABLE_AI}>
    ENABLE_V8=$<BOOL:${ENABLE_V8}>
    ENGINE_HAS_V8=$<BOOL:${ENABLE_V8}>
    ENGINE_HAS_CUDA_BRIDGE=${ENGINE_HAS_CUDA_BRIDGE}
    ENGINE_HAS_CUDNN_BRIDGE=${ENGINE_HAS_CUDNN_BRIDGE}
    ENGINE_HAS_ZSTD=${ENGINE_HAS_ZSTD}
    ENGINE_HAS_BROTLI=${ENGINE_HAS_BROTLI}
    ENGINE_HAS_FFI=${ENGINE_HAS_FFI}
    ENGINE_HAS_FFTW3F=${ENGINE_HAS_FFTW3F}
    ENGINE_HAS_HDF5=${ENGINE_HAS_HDF5}
    ENGINE_HAS_ICU=${ENGINE_HAS_ICU}
    ENGINE_HAS_SQLITE3=${ENGINE_HAS_SQLITE3}
    ENGINE_HAS_QT6=${ENGINE_HAS_QT6})

if(ENGINE_HAS_LIBUSB)
    target_include_directories(XER PRIVATE ${LIBUSB_INCLUDE_DIR})
    target_link_libraries(XER PRIVATE ${LIBUSB_LIBRARY})
    if(WIN32)
        target_link_libraries(XER PRIVATE setupapi cfgmgr32 winusb)
    elseif(APPLE)
        find_library(IOKIT_FRAMEWORK IOKit)
        find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
        target_link_libraries(XER PRIVATE ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    endif()
endif()
target_compile_definitions(XER PRIVATE ENGINE_HAS_LIBUSB=${ENGINE_HAS_LIBUSB})
if(TARGET XERLibusbExternal)
    add_dependencies(XER XERLibusbExternal)
endif()
if(TARGET XEROpenSSLExternal)
    add_dependencies(XER XEROpenSSLExternal)
endif()
if(TARGET XERV8External)
    add_dependencies(XER XERV8External)
endif()

# ── Required link deps ───────────────────────────────────────
target_link_libraries(XER PRIVATE
    XERAsyncIO
    XERJavaScript
    V8::V8
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    absl::strings
    absl::str_format
    absl::hash
    Eigen3::Eigen
    pthreadpool
    ${LLVM_ALL_COMPONENT_LIBS}
    ${CLANG_AVAILABLE_LIBS})

if(CMAKE_DL_LIBS)
    target_link_libraries(XER PRIVATE ${CMAKE_DL_LIBS})
endif()

# ── Optional system libs ─────────────────────────────────────
if(ENGINE_HAS_ZSTD)
    target_link_libraries(XER PRIVATE ${ZSTD_LIBRARY})
endif()

if(ENGINE_HAS_BROTLI)
    target_link_libraries(XER PRIVATE
        ${BROTLI_ENC_LIBRARY}
        ${BROTLI_DEC_LIBRARY}
        ${BROTLI_COMMON_LIBRARY})
endif()

if(ENGINE_HAS_FFI)
    target_link_libraries(XER PRIVATE ${FFI_LIBRARY})
endif()

if(ENGINE_HAS_FFTW3F)
    target_link_libraries(XER PRIVATE ${FFTW3F_LIBRARY})
endif()

if(ENGINE_HAS_HDF5)
    target_link_libraries(XER PRIVATE ${HDF5_LIBRARIES})
endif()

if(ENGINE_HAS_ICU)
    target_link_libraries(XER PRIVATE ${ICU_LIBRARIES})
endif()

if(TARGET LibXml2::LibXml2)
    target_link_libraries(XER PRIVATE LibXml2::LibXml2)
else()
    target_link_libraries(XER PRIVATE ${LIBXML2_LIBRARIES})
endif()

if(SQLite3_FOUND)
    target_link_libraries(XER PRIVATE SQLite::SQLite3)
endif()

# libuv
if(libuv_FOUND AND TARGET libuv::libuv)
    target_link_libraries(XER PRIVATE libuv::libuv)
else()
    pkg_check_modules(LIBUV libuv QUIET)
    if(LIBUV_FOUND)
        target_include_directories(XER PRIVATE ${LIBUV_INCLUDE_DIRS})
        target_link_libraries(XER PRIVATE ${LIBUV_LIBRARIES})
    endif()
endif()

# jsoncpp
if(TARGET JsonCpp::JsonCpp)
    target_link_libraries(XER PRIVATE JsonCpp::JsonCpp)
elseif(TARGET jsoncpp_lib)
    target_link_libraries(XER PRIVATE jsoncpp_lib)
else()
    pkg_check_modules(JSONCPP jsoncpp QUIET)
    if(JSONCPP_FOUND)
        target_include_directories(XER PRIVATE ${JSONCPP_INCLUDE_DIRS})
        target_link_libraries(XER PRIVATE ${JSONCPP_LIBRARIES})
    endif()
endif()

# re2
if(TARGET re2::re2)
    target_link_libraries(XER PRIVATE re2::re2)
else()
    pkg_check_modules(RE2 re2 QUIET)
    if(RE2_FOUND)
        target_include_directories(XER PRIVATE ${RE2_INCLUDE_DIRS})
        target_link_libraries(XER PRIVATE ${RE2_LIBRARIES})
    endif()
endif()

# range-v3
if(TARGET range-v3::range-v3)
    target_link_libraries(XER PRIVATE range-v3::range-v3)
elseif(TARGET range-v3)
    target_link_libraries(XER PRIVATE range-v3)
else()
    pkg_check_modules(RANGEV3 range-v3 QUIET)
    if(RANGEV3_FOUND)
        target_include_directories(XER PRIVATE ${RANGEV3_INCLUDE_DIRS})
        target_link_libraries(XER PRIVATE ${RANGEV3_LIBRARIES})
    endif()
endif()

if(TARGET absl::flat_hash_map)
    target_link_libraries(XER PRIVATE absl::flat_hash_map)
endif()

# ── Bridge static libs ───────────────────────────────────────
foreach(_bridge XERQt6Bridge XERJavaScript
                XEROpenCV XERBridgeCuda XERBridgeCudnn
                XERBridgeSkia XERBridgeFfmpeg XERBridgeAngle)
    if(TARGET ${_bridge})
        target_link_libraries(XER PRIVATE ${_bridge})
    endif()
endforeach()

if(Qt6Core_FOUND)
    target_link_libraries(XER PRIVATE Qt6::Core)
endif()

elseif(NOT ENABLE_V8)
    message(STATUS "ENABLE_V8=OFF: XER executable and JavaScript runtime target are disabled")
else()
    message(STATUS "XER executable disabled: LLVM and Clang CMake packages are required")
endif()

set(XER_AUDIO_ANALYSIS_SMOKE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/demo/audio_analysis_cli.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/demo/audio_analysis_smoke.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/audio_core/audio_interleave_processor.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/audio_core/audio_sample_rate_converter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/audio_core/audio_source_loader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/analysis_ai/anal_beat_tracker.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/analysis_ai/anal_loudness_meter_lufs.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/analysis_ai/anal_pitch_estimator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/analysis_ai/audio_fft_analyzer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/analysis_ai/audio_pitch_detector.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp_algorithms/dsp_fft_engine.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp_algorithms/dsp_windowing_functions.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/file_io_codecs/codec_wav_pcm.cc)

add_executable(XERAudioAnalysisSmoke ${XER_AUDIO_ANALYSIS_SMOKE_SOURCES})
target_include_directories(XERAudioAnalysisSmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ENGINE_FFMPEG_INCLUDE_DIRS})
target_compile_definitions(XERAudioAnalysisSmoke PRIVATE
    ENGINE_HAS_FFTW3F=${ENGINE_HAS_FFTW3F}
    ENGINE_HAS_FFMPEG_BRIDGE=${ENGINE_HAS_FFMPEG_BRIDGE})
target_link_libraries(XERAudioAnalysisSmoke PRIVATE
    XERAsyncIO
    XERBridgeFfmpeg)
if(ENGINE_HAS_FFTW3F)
    target_include_directories(XERAudioAnalysisSmoke PRIVATE ${FFTW3_INCLUDE_DIR})
    target_link_libraries(XERAudioAnalysisSmoke PRIVATE ${FFTW3F_LIBRARY})
endif()
if(ENGINE_FFMPEG_LIBRARIES)
    target_link_libraries(XERAudioAnalysisSmoke PRIVATE ${ENGINE_FFMPEG_LIBRARIES})
endif()
if(TARGET XERFFmpegExternal)
    add_dependencies(XERAudioAnalysisSmoke XERFFmpegExternal)
endif()

# ── Interface alias for all Qt6 targets ──────────────────────
if(ENGINE_QT6_FOUND_TARGETS)
    add_library(EngineQt6All INTERFACE)
    target_link_libraries(EngineQt6All INTERFACE ${ENGINE_QT6_FOUND_TARGETS})
endif()

function(add_engine_clap_plugin target_name source_file)
    add_library(${target_name} MODULE ${source_file})
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(${target_name} PROPERTIES
        PREFIX ""
        SUFFIX ".clap")
endfunction()

add_engine_clap_plugin(EngineClapSmokePlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/demo_app/clap_smoke_plugin.cc)
add_engine_clap_plugin(EngineClapEqPlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/demo_app/clap_eq_plugin.cc)
add_engine_clap_plugin(EngineClapBassBoostPlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/demo_app/clap_bass_boost_plugin.cc)
add_engine_clap_plugin(EngineClapPitchShifterPlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/demo_app/clap_pitch_shifter_plugin.cc)

add_executable(FluxTerminalManagerSmoke
    ${CMAKE_CURRENT_SOURCE_DIR}/demo_app/terminal_manager_smoke.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_interface.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_buffer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_emulator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_output_renderer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_ansi_parser.cc)
target_include_directories(FluxTerminalManagerSmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(FluxTerminalAnsiSmoke
    ${CMAKE_CURRENT_SOURCE_DIR}/demo_app/terminal_ansi_smoke.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_interface.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_buffer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_emulator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_output_renderer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_ansi_parser.cc)
target_include_directories(FluxTerminalAnsiSmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(FluxCoreSmoke
    ${CMAKE_CURRENT_SOURCE_DIR}/demo_app/flux_core_smoke.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/helper/string.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/core/flux_config.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/core/flux_context.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/core/flux_core.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/core/logger.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/core/module_manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_interface.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_buffer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_emulator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_output_renderer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_ansi_parser.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_input_handler.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_size_detector.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_window.cc)
target_include_directories(FluxCoreSmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(ENGINE_HAS_ICU)
    target_include_directories(FluxCoreSmoke PRIVATE ${ICU_INCLUDE_DIRS})
endif()
target_link_libraries(FluxCoreSmoke PRIVATE absl::strings)
if(ENGINE_HAS_ICU)
    target_link_libraries(FluxCoreSmoke PRIVATE ${ICU_LIBRARIES})
endif()
if(TARGET range-v3::range-v3)
    target_link_libraries(FluxCoreSmoke PRIVATE range-v3::range-v3)
elseif(TARGET range-v3)
    target_link_libraries(FluxCoreSmoke PRIVATE range-v3)
endif()

add_executable(FluxUiSmoke
    ${CMAKE_CURRENT_SOURCE_DIR}/demo_app/flux_ui_smoke.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/history/action_history.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/history/command_history.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/input/command_parser.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/input/input_manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/input/key_binding_manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/rendering/frame_buffer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/rendering/render_context.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/rendering/renderer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/rendering/render_pipeline.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_interface.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_buffer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_emulator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_output_renderer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_ansi_parser.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_input_handler.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_size_detector.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/flux/terminal/terminal_window.cc)
target_include_directories(FluxUiSmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(XERDspMemoryAsyncZeroCopySmoke
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/dsp_memory_async_zero_copy_test.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp_algorithms/dsp_fir_filter_bank.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp_algorithms/dsp_convolution_engine.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp_algorithms/dsp_biquad_processor.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp_algorithms/dsp_delay_line_circular.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/effects_rack/fx_eq_parametric.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/effects_rack/fx_mod_phaser.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/effects_rack/fx_mod_flanger.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/effects_rack/fx_mod_chorus.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/effects_rack/fx_reverb_algorithmic.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/async_io/sync_primitives/mutex_wrapper.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/async_io/async_buffer_pool.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/async_io/log_and_debug/io_perf_counter.cc)
target_include_directories(XERDspMemoryAsyncZeroCopySmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
