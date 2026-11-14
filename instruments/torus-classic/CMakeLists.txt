# Torus Classic: VST3 instrument around the dry Rings/Torus keyboard engine of
# the Daisy Seed3 firmware "Physical Modeling". Four layers, each a target:
#
#   torus_classic_core     hardware-free Torus DSP and its stmlib parts (vendored)
#   torus_classic_engine   dry instrument: notes, voices, parameters, output stage
#   torus_classic_adapter  streaming: sample rate, 12-sample quanta, event time
#   torus_classic_shell    VST3 processor, controller, state, editor
#
# Its own version; moduleinfo.json takes it from here. Keep it equal to
# FULL_VERSION_STR in source/version.h and to components.json.
project(TorusClassic VERSION 0.1.1 LANGUAGES CXX)

# The SDK compiles everything with /fp:fast. The DSP, the adapter and their
# tests use /fp:precise instead: the engine's guard against non-finite samples
# and the bit-exact comparison with the reference both rely on IEEE semantics.
set(TORUS_CLASSIC_FP /fp:precise)

# --- Layer 1: vendored DSP --------------------------------------------------
# Copied from Daisy-Seed with the patches listed in UPSTREAM.md. TEST selects
# stmlib's portable C++ instead of ARM assembly. Warnings of the upstream code
# about implicit float conversions are switched off for this target only.
add_library(torus_classic_core STATIC
    core/stmlib/stmlib.h
    core/stmlib/dsp/cosine_oscillator.h
    core/stmlib/dsp/delay_line.h
    core/stmlib/dsp/dsp.h
    core/stmlib/dsp/filter.h
    core/stmlib/dsp/parameter_interpolator.h
    core/stmlib/dsp/units.h
    core/stmlib/dsp/units.cc
    core/torus/resources.h
    core/torus/resources.cpp
    core/torus/dsp/dsp.h
    core/torus/dsp/fm_voice.h
    core/torus/dsp/fm_voice.cpp
    core/torus/dsp/follower.h
    core/torus/dsp/limiter.h
    core/torus/dsp/note_filter.h
    core/torus/dsp/optimization.h
    core/torus/dsp/part.h
    core/torus/dsp/part.cpp
    core/torus/dsp/patch.h
    core/torus/dsp/performance_state.h
    core/torus/dsp/plucker.h
    core/torus/dsp/resonator.h
    core/torus/dsp/resonator.cpp
    core/torus/dsp/rng.h
    core/torus/dsp/string.h
    core/torus/dsp/string.cpp)
target_include_directories(torus_classic_core PUBLIC core/torus core)
target_compile_definitions(torus_classic_core PUBLIC TEST _USE_MATH_DEFINES)
set(TORUS_CLASSIC_UPSTREAM_WARNINGS /wd4244 /wd4305 /wd4267 /wd4018 /wd4100 /wd4456 /wd4458)
target_compile_options(torus_classic_core PRIVATE ${TORUS_CLASSIC_FP} ${TORUS_CLASSIC_UPSTREAM_WARNINGS})
set_target_properties(torus_classic_core PROPERTIES FOLDER "Torus Classic")

# --- Layer 2: dry instrument engine -----------------------------------------
# Only dry_engine.h is meant for the layers above; it shows no DSP type, and
# the core's include paths and macros stay private to this target.
add_library(torus_classic_engine STATIC
    engine/audio_output.h
    engine/config.h
    engine/dry_engine.cpp
    engine/dry_engine.h
    engine/parameter_manager.h
    engine/synth.cpp
    engine/synth.h)
target_include_directories(torus_classic_engine PUBLIC engine)
target_compile_options(torus_classic_engine PRIVATE ${TORUS_CLASSIC_FP} ${TORUS_CLASSIC_UPSTREAM_WARNINGS})
target_link_libraries(torus_classic_engine PRIVATE torus_classic_core)
set_target_properties(torus_classic_engine PROPERTIES FOLDER "Torus Classic")

# --- Layer 3: streaming adapter ---------------------------------------------
# Sample rate conversion, the 12-sample quantum FIFO and event time. Knows the
# engine only as the QuantumEngine interface declared in its own header.
add_library(torus_classic_adapter STATIC
    adapter/sinc_kernel.cpp
    adapter/sinc_kernel.h
    adapter/stream_adapter.cpp
    adapter/stream_adapter.h)
target_include_directories(torus_classic_adapter PUBLIC adapter)
target_compile_options(torus_classic_adapter PRIVATE ${TORUS_CLASSIC_FP} /W4 /permissive-)
set_target_properties(torus_classic_adapter PROPERTIES FOLDER "Torus Classic")

# --- Layer 4: the VST3 plug-in ----------------------------------------------
# Without its factory, so that the tests can drive it in-process.
add_library(torus_classic_shell STATIC
    source/controller.cpp
    source/controller.h
    source/editor.cpp
    source/editor.h
    source/engine_bridge.h
    source/latest_value.h
    source/parameters.h
    source/plugin_ids.h
    source/processor.cpp
    source/processor.h
    source/spsc_queue.h
    source/state_io.h
    source/value_text.h
    source/version.h
    source/view_layout.h
    source/widgets.cpp
    source/widgets.h)
target_include_directories(torus_classic_shell PUBLIC source)
target_compile_options(torus_classic_shell PRIVATE ${TORUS_CLASSIC_FP} /W4 /permissive-)
target_compile_definitions(torus_classic_shell PUBLIC NOMINMAX)
target_link_libraries(torus_classic_shell PUBLIC
    torus_classic_adapter torus_classic_engine sdk vstgui_support)
set_target_properties(torus_classic_shell PROPERTIES FOLDER "Torus Classic")

smtg_add_vst3plugin(TorusClassic
    PACKAGE_NAME "Torus Classic"
    SOURCES_LIST source/factory.cpp)
target_link_libraries(TorusClassic PRIVATE torus_classic_shell sdk)
set_target_properties(TorusClassic PROPERTIES FOLDER "Torus Classic")

if(BUILD_TESTING)
    # The pinned firmware engine, reverb included, for the dry reference
    # comparison. Verbatim copies; MSVC rejects two empty arrays in the
    # generated resources.cpp, so that one file is compiled from a copy with
    # exactly those two definitions removed. Namespaces are renamed so that
    # reference and port share no symbol.
    set(ref "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference")
    file(READ "${ref}/torus/resources.cpp" reference_resources)
    string(REPLACE "const int16_t* lookup_table_int16_table[] = {};" ""
        reference_resources "${reference_resources}")
    string(REPLACE "const uint32_t* lookup_table_uint32_table[] = {};" ""
        reference_resources "${reference_resources}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reference/resources_msvc.cpp"
        CONTENT "${reference_resources}" @ONLY NEWLINE_STYLE LF)
    add_library(torus_classic_reference STATIC
        tests/reference/reference_api.cpp
        tests/reference/reference_api.h
        tests/reference/src/physical_modeling.cpp
        tests/reference/torus/dsp/part.cpp
        tests/reference/torus/dsp/fm_voice.cpp
        tests/reference/torus/dsp/string.cpp
        tests/reference/torus/dsp/resonator.cpp
        tests/reference/third_party/stmlib/dsp/units.cc
        tests/reference/third_party/stmlib/utils/random.cc
        "${CMAKE_CURRENT_BINARY_DIR}/reference/resources_msvc.cpp")
    target_include_directories(torus_classic_reference
        PUBLIC tests/reference
        PRIVATE tests/reference/src tests/reference/torus tests/reference/third_party)
    target_compile_definitions(torus_classic_reference PRIVATE
        TEST _USE_MATH_DEFINES
        torus=torus_reference stmlib=stmlib_reference
        physical_modeling=physical_modeling_reference)
    target_compile_options(torus_classic_reference PRIVATE
        ${TORUS_CLASSIC_FP} ${TORUS_CLASSIC_UPSTREAM_WARNINGS})

    add_executable(test_torus_classic_engine tests/test_engine.cpp tests/test_support.h)
    target_compile_options(test_torus_classic_engine PRIVATE ${TORUS_CLASSIC_FP} /W4 /permissive-)
    target_link_libraries(test_torus_classic_engine PRIVATE
        torus_classic_engine torus_classic_reference)
    add_test(NAME torus-classic.engine COMMAND test_torus_classic_engine)

    add_executable(test_torus_classic_adapter tests/test_adapter.cpp tests/test_support.h)
    target_include_directories(test_torus_classic_adapter PRIVATE source)
    target_compile_options(test_torus_classic_adapter PRIVATE ${TORUS_CLASSIC_FP} /W4 /permissive-)
    target_link_libraries(test_torus_classic_adapter PRIVATE
        torus_classic_adapter torus_classic_engine)
    add_test(NAME torus-classic.adapter COMMAND test_torus_classic_adapter)

    add_executable(test_torus_classic_plugin
        tests/test_plugin.cpp
        tests/test_support.h
        "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
    target_compile_options(test_torus_classic_plugin PRIVATE ${TORUS_CLASSIC_FP} /permissive-)
    target_link_libraries(test_torus_classic_plugin PRIVATE
        torus_classic_shell torus_classic_reference sdk_hosting)
    add_test(NAME torus-classic.plugin COMMAND test_torus_classic_plugin)

    # The copies from Daisy-Seed are what the manifest and the hash list say.
    add_test(NAME torus-classic.provenance
        COMMAND "${CMAKE_COMMAND}" "-DPLUGIN_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DBUILD_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_provenance.cmake")

    # Measurements for the build report; run by hand, not a test.
    add_executable(bench_torus_classic tests/bench.cpp)
    target_include_directories(bench_torus_classic PRIVATE source)
    target_compile_options(bench_torus_classic PRIVATE ${TORUS_CLASSIC_FP} /W4 /permissive-)
    target_link_libraries(bench_torus_classic PRIVATE torus_classic_adapter torus_classic_engine)

    set_target_properties(torus_classic_reference test_torus_classic_engine
        test_torus_classic_adapter test_torus_classic_plugin bench_torus_classic
        PROPERTIES FOLDER "Torus Classic")
endif()
