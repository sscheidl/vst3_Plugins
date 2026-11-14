# Mendelejew (internal project name: Elements): VST3 instrument around the
# modal synthesis DSP of the Mutable Instruments Elements module, running at a
# true 48 kHz. Only the plug-in target carries the product name. Four layers,
# each a target:
#
#   elements_core     the DSP, adapted to a run-time sample rate (vendored)
#   elements_engine   instrument: notes, controls, smoothing, output
#   elements_adapter  streaming: sample rate, 24-sample quanta, event time
#   elements_shell    VST3 processor, controller, state, editor
#
# Its own version; moduleinfo.json takes it from here. Keep it equal to
# FULL_VERSION_STR in source/version.h.
project(Elements VERSION 0.1.0 LANGUAGES CXX)

# The SDK compiles everything with /fp:fast. The DSP, the engine, the adapter
# and their tests use /fp:precise instead: the engine's guard against
# non-finite samples and the bit-exact comparison with the original code both
# rely on IEEE semantics.
set(ELEMENTS_FP /fp:precise)
# Warnings of the upstream code about implicit conversions and shadowing are
# switched off for the targets that compile it, and only for those.
set(ELEMENTS_UPSTREAM_WARNINGS /wd4244 /wd4305 /wd4267 /wd4018 /wd4100 /wd4456 /wd4458 /wd4459 /wd4127 /wd4701 /wd4715)

# resources.cc is kept as upstream generated it, 1.6 MB of tables and samples.
# It defines one empty array, which MSVC rejects; the build compiles a copy
# without that definition. Nothing refers to the array.
set(ELEMENTS_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/resources_msvc.cc")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/core/elements/resources.cc" elements_resources)
string(REGEX REPLACE "const uint32_t\\* lookup_table_uint32_table\\[\\] = {[ \r\n]*};" ""
    elements_resources_msvc "${elements_resources}")
if(elements_resources_msvc STREQUAL elements_resources)
    message(FATAL_ERROR "resources.cc: the empty array to remove for MSVC was not found")
endif()
file(CONFIGURE OUTPUT "${ELEMENTS_RESOURCES}" CONTENT "${elements_resources_msvc}" @ONLY
    NEWLINE_STYLE LF)
unset(elements_resources)
unset(elements_resources_msvc)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/elements/resources.cc")

# --- Layer 1: vendored DSP --------------------------------------------------
# From pichenettes/eurorack and pichenettes/stmlib with the patches listed in
# UPSTREAM.md. TEST selects stmlib's portable C++ instead of ARM assembly.
add_library(elements_core STATIC
    core/stmlib/stmlib.h
    core/stmlib/dsp/cosine_oscillator.h
    core/stmlib/dsp/delay_line.h
    core/stmlib/dsp/dsp.h
    core/stmlib/dsp/filter.h
    core/stmlib/dsp/units.h
    core/stmlib/dsp/units.cc
    core/elements/resources.h
    "${ELEMENTS_RESOURCES}"
    core/elements/dsp/dsp.h
    core/elements/dsp/exciter.h
    core/elements/dsp/exciter.cc
    core/elements/dsp/fx/diffuser.h
    core/elements/dsp/fx/fx_engine.h
    core/elements/dsp/fx/reverb.h
    core/elements/dsp/multistage_envelope.h
    core/elements/dsp/multistage_envelope.cc
    core/elements/dsp/part.h
    core/elements/dsp/part.cc
    core/elements/dsp/patch.h
    core/elements/dsp/resonator.h
    core/elements/dsp/resonator.cc
    core/elements/dsp/rng.h
    core/elements/dsp/sample_rate.h
    core/elements/dsp/sample_rate.cc
    core/elements/dsp/tube.h
    core/elements/dsp/tube.cc
    core/elements/dsp/voice.h
    core/elements/dsp/voice.cc)
target_include_directories(elements_core PUBLIC core)
target_compile_definitions(elements_core PUBLIC TEST _USE_MATH_DEFINES)
target_compile_options(elements_core PRIVATE ${ELEMENTS_FP} ${ELEMENTS_UPSTREAM_WARNINGS})
set_target_properties(elements_core PROPERTIES FOLDER "Elements")

# --- Layer 2: instrument engine ---------------------------------------------
# Only engine.h and controls.h are meant for the layers above; they show no
# DSP type, and the core's include paths and macros stay private to this
# target.
add_library(elements_engine STATIC
    engine/controls.h
    engine/engine.cpp
    engine/engine.h)
target_include_directories(elements_engine PUBLIC engine)
target_compile_options(elements_engine PRIVATE ${ELEMENTS_FP} ${ELEMENTS_UPSTREAM_WARNINGS})
target_link_libraries(elements_engine PRIVATE elements_core)
set_target_properties(elements_engine PROPERTIES FOLDER "Elements")

# --- Layer 3: streaming adapter ---------------------------------------------
# Sample rate conversion, the 24-sample quantum FIFO and event time. Knows the
# engine only as the QuantumEngine interface declared in its own header.
add_library(elements_adapter STATIC
    adapter/sinc_kernel.cpp
    adapter/sinc_kernel.h
    adapter/stream_adapter.cpp
    adapter/stream_adapter.h)
target_include_directories(elements_adapter PUBLIC adapter)
target_compile_options(elements_adapter PRIVATE ${ELEMENTS_FP} /W4 /permissive-)
set_target_properties(elements_adapter PROPERTIES FOLDER "Elements")

# --- Layer 4: the VST3 plug-in ----------------------------------------------
# Without its factory, so that the tests can drive it in-process.
add_library(elements_shell STATIC
    source/controller.cpp
    source/controller.h
    source/editor.cpp
    source/editor.h
    source/engine_bridge.h
    source/latest_value.h
    source/parameter_state.h
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
target_include_directories(elements_shell PUBLIC source)
target_compile_options(elements_shell PRIVATE ${ELEMENTS_FP} /W4 /permissive-)
target_compile_definitions(elements_shell PUBLIC NOMINMAX)
target_link_libraries(elements_shell PUBLIC
    elements_adapter elements_engine sdk vstgui_support)
set_target_properties(elements_shell PROPERTIES FOLDER "Elements")

# The target name is the name of the bundle and of the binary in it.
smtg_add_vst3plugin(Mendelejew
    PACKAGE_NAME "Mendelejew"
    SOURCES_LIST source/factory.cpp)
target_link_libraries(Mendelejew PRIVATE elements_shell sdk)
set_target_properties(Mendelejew PROPERTIES FOLDER "Elements")

if(BUILD_TESTING)
    # The original DSP at its 32 kHz, for the comparison with the port.
    # Verbatim copies; the namespaces are renamed so that reference and port
    # share no symbol. The tables and samples are the one resources.cc.
    set(ref tests/reference/eurorack)
    add_library(elements_reference STATIC
        tests/reference/reference_api.cpp
        tests/reference/reference_api.h
        ${ref}/elements/dsp/exciter.cc
        ${ref}/elements/dsp/multistage_envelope.cc
        ${ref}/elements/dsp/ominous_voice.cc
        ${ref}/elements/dsp/part.cc
        ${ref}/elements/dsp/resonator.cc
        ${ref}/elements/dsp/string.cc
        ${ref}/elements/dsp/tube.cc
        ${ref}/elements/dsp/voice.cc
        ${ref}/stmlib/dsp/units.cc
        ${ref}/stmlib/utils/random.cc
        "${ELEMENTS_RESOURCES}")
    target_include_directories(elements_reference
        PUBLIC tests/reference
        PRIVATE ${ref})
    target_compile_definitions(elements_reference PRIVATE
        TEST _USE_MATH_DEFINES
        elements=elements_reference stmlib=stmlib_reference)
    target_compile_options(elements_reference PRIVATE
        ${ELEMENTS_FP} ${ELEMENTS_UPSTREAM_WARNINGS})

    # The DSP itself: bit-exact with the original at 32 kHz, and measured
    # against it at 48 and 96 kHz.
    add_executable(test_elements_dsp tests/test_dsp.cpp tests/test_support.h)
    target_compile_options(test_elements_dsp PRIVATE ${ELEMENTS_FP} ${ELEMENTS_UPSTREAM_WARNINGS})
    target_link_libraries(test_elements_dsp PRIVATE elements_core elements_reference)
    add_test(NAME elements.dsp COMMAND test_elements_dsp
        "${CMAKE_CURRENT_BINARY_DIR}/dsp-measurements-$<CONFIG>.md")

    # The instrument engine and the streaming adapter, without VST3.
    add_executable(test_elements_engine tests/test_engine.cpp tests/test_support.h)
    target_include_directories(test_elements_engine PRIVATE source)
    target_compile_options(test_elements_engine PRIVATE ${ELEMENTS_FP} /W4 /permissive-)
    target_link_libraries(test_elements_engine PRIVATE elements_adapter elements_engine)
    add_test(NAME elements.engine COMMAND test_elements_engine)

    # The plug-in as a host sees it.
    add_executable(test_elements_plugin
        tests/test_plugin.cpp
        tests/test_support.h
        "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
    target_compile_options(test_elements_plugin PRIVATE ${ELEMENTS_FP} /permissive-)
    target_link_libraries(test_elements_plugin PRIVATE elements_shell sdk_hosting)
    add_test(NAME elements.plugin COMMAND test_elements_plugin)

    # The copies from upstream are what SHA256SUMS.upstream says.
    add_test(NAME elements.provenance
        COMMAND "${CMAKE_COMMAND}" "-DPLUGIN_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_provenance.cmake")

    set_target_properties(elements_reference test_elements_dsp test_elements_engine
        test_elements_plugin PROPERTIES FOLDER "Elements")
endif()
