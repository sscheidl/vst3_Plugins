# Messier: VST3 instrument around the granular looper of the Daisy Seed3 firmware
# "Nebulae on Daisy" 0.2.2 (Daisy-Seed instruments/granular). File source only in
# 0.1.0. Four layers, each a target:
#
#   messier_core      the hardware-free firmware DSP (vendored, see UPSTREAM.md) and
#                     this repository's FFT backend for it
#   messier_engine    the looper engine: drives neb::Engine through its public interface
#   messier_adapter   streaming: sample rate, 64-sample quanta, event time
#   messier_shell     VST3 processor, controller, state, editor, file loader
#
# Its own version; moduleinfo.json takes it from here. Keep it equal to
# FULL_VERSION_STR in source/version.h and to components.json.
project(Messier VERSION 0.2.0 LANGUAGES CXX)

# The SDK compiles everything with /fp:fast. The DSP, the adapter and their tests
# use /fp:precise: the bit-exact comparison with the reference needs IEEE semantics.
set(MESSIER_FP /fp:precise)
# Warnings of the upstream code about implicit conversions, for its targets only.
set(MESSIER_UPSTREAM_WARNINGS /wd4244 /wd4305 /wd4267 /wd4018 /wd4100 /wd4456 /wd4458 /wd4189)

# --- Layer 1: vendored DSP --------------------------------------------------
# reference_dsp.* are LGPL-2.1-or-later (the Csound vocoder), the rest of the copies
# MIT; see licenses/README.md and UPSTREAM.md. fft_desktop.cpp is this repository's.
add_library(messier_core STATIC
    core/control.cpp
    core/control.h
    core/engine.cpp
    core/engine.h
    core/fft.h
    core/fft_desktop.cpp
    core/pcm.h
    core/profile.h
    core/reference_dsp.cpp
    core/reference_dsp.h
    core/wav.h)
target_include_directories(messier_core PUBLIC core)
target_compile_definitions(messier_core PUBLIC NEBULAE_MOD=0 NEBULAE_PROFILE=0)
target_compile_options(messier_core PRIVATE ${MESSIER_FP} ${MESSIER_UPSTREAM_WARNINGS})
set_target_properties(messier_core PROPERTIES FOLDER "Messier")

# --- Layer 2: looper engine -------------------------------------------------
# Only looper_engine.h is meant for the layers above; it shows no DSP type, and
# the core's include paths and macros stay private to this target.
add_library(messier_engine STATIC
    engine/looper_engine.cpp
    engine/looper_engine.h)
target_include_directories(messier_engine PUBLIC engine)
target_compile_options(messier_engine PRIVATE ${MESSIER_FP} /W4 /permissive-)
target_link_libraries(messier_engine PRIVATE messier_core)
set_target_properties(messier_engine PROPERTIES FOLDER "Messier")

# --- Layer 3: streaming adapter ---------------------------------------------
add_library(messier_adapter STATIC
    adapter/sinc_kernel.cpp
    adapter/sinc_kernel.h
    adapter/stream_adapter.cpp
    adapter/stream_adapter.h)
target_include_directories(messier_adapter PUBLIC adapter)
target_compile_options(messier_adapter PRIVATE ${MESSIER_FP} /W4 /permissive-)
set_target_properties(messier_adapter PROPERTIES FOLDER "Messier")

# --- Layer 4: the VST3 plug-in ----------------------------------------------
# Without its factory, so that the tests can drive it in-process.
add_library(messier_shell STATIC
    source/controller.cpp
    source/controller.h
    source/editor.cpp
    source/display_data.cpp
    source/display_data.h
    source/editor.h
    source/engine_bridge.h
    source/file_loader.cpp
    source/file_loader.h
    source/latest_value.h
    source/file_drop.cpp
    source/file_drop.h
    source/gui_layout.h
    source/gui_skin.cpp
    source/gui_skin.h
    source/map_view.cpp
    source/map_view.h
    source/panel_view.cpp
    source/panel_view.h
    source/parameter_state.h
    source/parameters.h
    source/plugin_ids.h
    source/processor.cpp
    source/processor.h
    source/spsc_queue.h
    source/state_io.h
    source/value_display.h
    source/value_text.h
    source/version.h
    source/view_layout.h
    source/wav_reader.cpp
    source/wav_reader.h
    source/wave_overview.cpp
    source/wave_overview.h
    source/widgets.cpp
    source/widgets.h)
target_include_directories(messier_shell PUBLIC source)
target_compile_options(messier_shell PRIVATE ${MESSIER_FP} /W4 /permissive-)
target_compile_definitions(messier_shell PUBLIC NOMINMAX)
target_link_libraries(messier_shell PUBLIC
    messier_adapter messier_engine sdk vstgui_support)
# The window draws the core's own grain windows as the WINDOW symbol (since 0.1.8).
target_link_libraries(messier_shell PRIVATE messier_core)
set_target_properties(messier_shell PROPERTIES FOLDER "Messier")

smtg_add_vst3plugin(Messier
    PACKAGE_NAME "Messier"
    SOURCES_LIST source/factory.cpp)
target_link_libraries(Messier PRIVATE messier_shell sdk)
set_target_properties(Messier PROPERTIES FOLDER "Messier")

# The window's bitmaps (0.2.0): surfaces and materials of the GUI presets at 1x and 2x,
# painted by Codex/GPT after docs/GRAFIKAUFTRAG_GUI.md, into the bundle's
# Contents/Resources. Only Slate is delivered so far; gui_skin.cpp knows the names.
set(MESSIER_GUI_IMAGES
    messier_slate_chassis messier_slate_bezel messier_slate_plate messier_nebula_a
    messier_nebula_b messier_slate_knob_l messier_slate_knob_s messier_slate_thumb
    messier_slate_button messier_slate_button_down messier_slate_screw messier_keys_white
    messier_keys_black)
set(MESSIER_GUI_BITMAPS "")
foreach(image IN LISTS MESSIER_GUI_IMAGES)
    list(APPEND MESSIER_GUI_BITMAPS
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/gui/${image}@1x.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/gui/${image}@2x.png")
endforeach()
smtg_target_add_plugin_resources(Messier RESOURCES ${MESSIER_GUI_BITMAPS})

if(BUILD_TESTING)
    # The firmware's own vocoder test, unchanged: this FFT backend against the
    # radix-2 reference, PcmReader against PcmView, Mincer against ReferenceMincer.
    # C4146: the test negates an unsigned loop index (-k*n), so those values come out
    # positive; left as it is, the copy is verbatim.
    add_executable(test_messier_vocoder tests/core/test_vocoder.cpp)
    target_compile_options(test_messier_vocoder PRIVATE ${MESSIER_FP} ${MESSIER_UPSTREAM_WARNINGS} /wd4146)
    target_link_libraries(test_messier_vocoder PRIVATE messier_core)
    add_test(NAME messier.vocoder COMMAND test_messier_vocoder)

    # The pinned firmware core for the reference comparison: the copies in
    # tests/reference differ from Daisy-Seed only by the F42 line (a test checks
    # that), and are compiled with the namespace neb renamed, so that reference and
    # port share no symbol. They get the same FFT backend as the plug-in, the only
    # one this repository has; test_messier_vocoder checks it against the core's own
    # radix-2 reference. reference_api.cpp is the test's own driver.
    add_library(messier_reference STATIC
        tests/reference/control.cpp
        tests/reference/engine.cpp
        tests/reference/reference_dsp.cpp
        core/fft_desktop.cpp
        tests/reference_api.cpp
        tests/reference_api.h)
    target_include_directories(messier_reference
        PUBLIC tests
        PRIVATE tests/reference)
    target_compile_definitions(messier_reference PRIVATE
        neb=neb_reference NEBULAE_MOD=0 NEBULAE_PROFILE=0)
    target_compile_options(messier_reference PRIVATE ${MESSIER_FP} ${MESSIER_UPSTREAM_WARNINGS})

    add_executable(test_messier_engine tests/test_engine.cpp tests/test_support.h)
    target_include_directories(test_messier_engine PRIVATE source)
    target_compile_options(test_messier_engine PRIVATE ${MESSIER_FP} /W4 /permissive-)
    target_link_libraries(test_messier_engine PRIVATE
        messier_engine messier_core messier_reference)
    add_test(NAME messier.engine COMMAND test_messier_engine)

    add_executable(test_messier_adapter tests/test_adapter.cpp tests/test_support.h)
    target_compile_options(test_messier_adapter PRIVATE ${MESSIER_FP} /W4 /permissive-)
    target_link_libraries(test_messier_adapter PRIVATE messier_adapter)
    add_test(NAME messier.adapter COMMAND test_messier_adapter)

    add_executable(test_messier_loader tests/test_loader.cpp tests/test_support.h)
    target_compile_options(test_messier_loader PRIVATE ${MESSIER_FP} /W4 /permissive-)
    target_link_libraries(test_messier_loader PRIVATE messier_shell)
    add_test(NAME messier.loader COMMAND test_messier_loader)

    add_executable(test_messier_plugin
        tests/test_plugin.cpp
        tests/test_support.h
        "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
    target_compile_options(test_messier_plugin PRIVATE ${MESSIER_FP} /permissive-)
    target_link_libraries(test_messier_plugin PRIVATE
        messier_shell messier_reference sdk_hosting)
    # The bitmaps the window asks for are those the bundle gets (0.2.0).
    target_compile_definitions(test_messier_plugin PRIVATE
        "MESSIER_GUI_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/resource/gui\"")
    add_test(NAME messier.plugin COMMAND test_messier_plugin)

    # The copies from Daisy-Seed are what the manifest and the hash list say, and the
    # reference differs from the firmware by the F42 line only.
    add_test(NAME messier.provenance
        COMMAND "${CMAKE_COMMAND}" "-DPLUGIN_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DBUILD_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_provenance.cmake")

    # Measurements for the build report; run by hand, not a test.
    add_executable(bench_messier tests/bench.cpp)
    target_include_directories(bench_messier PRIVATE source)
    target_compile_options(bench_messier PRIVATE ${MESSIER_FP} /W4 /permissive-)
    target_link_libraries(bench_messier PRIVATE messier_adapter messier_engine)

    set_target_properties(test_messier_vocoder messier_reference test_messier_engine
        test_messier_adapter test_messier_loader test_messier_plugin bench_messier
        PROPERTIES FOLDER "Messier")
endif()
