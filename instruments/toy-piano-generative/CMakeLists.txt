# Toy Piano Generative: the Toy Piano sound engine played by a generative step sequencer.

# Its own version; moduleinfo.json takes it from here. Keep it equal to
# FULL_VERSION_STR in source/version.h.
project(ToyPianoGenerative VERSION 0.2.0 LANGUAGES CXX)

# The sequencer on its own: plain C++, no SDK, no plug-in types. /fp:precise
# because the SDK's /fp:fast would let the compiler assume away the NaN check
# in the knob law.
add_library(generative_sequencer STATIC
    sequencer/generative_sequencer.cpp
    sequencer/generative_sequencer.h)
target_include_directories(generative_sequencer PUBLIC sequencer)
target_compile_options(generative_sequencer PRIVATE /W4 /permissive- /fp:precise)
set_target_properties(generative_sequencer PROPERTIES FOLDER "Toy Piano Generative")

# The plug-in without its factory, so that the tests can drive it in-process.
# The sound engine is the unchanged firmware engine of Toy Piano Classic:
# its target toy_piano_core, not a copy.
add_library(toy_piano_generative_shell STATIC
    source/controller.cpp
    source/controller.h
    source/editor.cpp
    source/editor.h
    source/engine.cpp
    source/engine.h
    source/handover.h
    source/keyboard_layout.h
    source/keyboard_view.cpp
    source/keyboard_view.h
    source/parameters.h
    source/plugin_ids.h
    source/processor.cpp
    source/processor.h
    source/state_io.h
    source/step_clock.h
    source/value_text.h
    source/version.h)
target_include_directories(toy_piano_generative_shell PUBLIC source)
# /fp:precise: the state check and the tuning rely on NaN and on exact products.
target_compile_options(toy_piano_generative_shell PRIVATE /W4 /permissive- /fp:precise)
target_compile_definitions(toy_piano_generative_shell PUBLIC NOMINMAX)
target_link_libraries(toy_piano_generative_shell
    PUBLIC toy_piano_core generative_sequencer sdk vstgui_support)
set_target_properties(toy_piano_generative_shell PROPERTIES FOLDER "Toy Piano Generative")

smtg_add_vst3plugin(ToyPianoGenerative
    PACKAGE_NAME "Toy Piano Generative"
    SOURCES_LIST source/factory.cpp)
target_link_libraries(ToyPianoGenerative PRIVATE toy_piano_generative_shell sdk)
set_target_properties(ToyPianoGenerative PROPERTIES FOLDER "Toy Piano Generative")

if(BUILD_TESTING)
    add_executable(test_generative_sequencer tests/test_sequencer.cpp)
    target_compile_options(test_generative_sequencer PRIVATE /W4 /permissive- /fp:precise)
    target_link_libraries(test_generative_sequencer PRIVATE generative_sequencer)
    add_test(NAME toy-piano-generative.sequencer COMMAND test_generative_sequencer)

    add_executable(test_toy_piano_generative_plugin
        tests/test_plugin.cpp
        "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
    target_compile_options(test_toy_piano_generative_plugin PRIVATE /permissive- /fp:precise)
    target_link_libraries(test_toy_piano_generative_plugin
        PRIVATE toy_piano_generative_shell sdk_hosting)
    add_test(NAME toy-piano-generative.plugin COMMAND test_toy_piano_generative_plugin)

    set_target_properties(test_generative_sequencer test_toy_piano_generative_plugin
        PROPERTIES FOLDER "Toy Piano Generative")
endif()
