# Toy Piano Classic: VST3 instrument around the unchanged Daisy Seed3 sound engine.

# Its own version; moduleinfo.json takes it from here. Keep it equal to
# FULL_VERSION_STR in source/version.h.
project(ToyPianoClassic VERSION 0.3.0 LANGUAGES CXX)

# The firmware engine exactly as copied from Daisy-Seed. Its own warnings stay
# as they are; the files are not to be edited here.
add_library(toy_piano_core STATIC
    core/config.h
    core/spsc_queue.h
    core/toy_piano.h
    core/toy_piano.cpp
    core/compat/Utility/dsp.h)
target_include_directories(toy_piano_core PUBLIC core PRIVATE core/compat)
set_target_properties(toy_piano_core PROPERTIES FOLDER "Toy Piano Classic")

# The plug-in without its factory, so that the tests can drive it in-process.
add_library(toy_piano_classic_shell STATIC
    source/controller.cpp
    source/controller.h
    source/editor.cpp
    source/editor.h
    source/engine.cpp
    source/engine.h
    source/keyboard_layout.h
    source/keyboard_view.cpp
    source/keyboard_view.h
    source/parameters.h
    source/plugin_ids.h
    source/processor.cpp
    source/processor.h
    source/version.h)
target_include_directories(toy_piano_classic_shell PUBLIC source)
target_compile_options(toy_piano_classic_shell PRIVATE /W4 /permissive-)
target_compile_definitions(toy_piano_classic_shell PUBLIC NOMINMAX)
target_link_libraries(toy_piano_classic_shell PUBLIC toy_piano_core sdk vstgui_support)
set_target_properties(toy_piano_classic_shell PROPERTIES FOLDER "Toy Piano Classic")

smtg_add_vst3plugin(ToyPianoClassic
    PACKAGE_NAME "Toy Piano Classic"
    SOURCES_LIST source/factory.cpp)
target_link_libraries(ToyPianoClassic PRIVATE toy_piano_classic_shell sdk)
set_target_properties(ToyPianoClassic PROPERTIES FOLDER "Toy Piano Classic")

if(BUILD_TESTING)
    add_executable(test_toy_piano_core tests/test_toy_piano_core.cpp)
    target_link_libraries(test_toy_piano_core PRIVATE toy_piano_core)
    add_test(NAME toy-piano-classic.core COMMAND test_toy_piano_core)

    add_executable(test_toy_piano_classic_plugin
        tests/test_plugin.cpp
        "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
    target_compile_options(test_toy_piano_classic_plugin PRIVATE /permissive-)
    target_link_libraries(test_toy_piano_classic_plugin PRIVATE toy_piano_classic_shell sdk_hosting)
    add_test(NAME toy-piano-classic.plugin COMMAND test_toy_piano_classic_plugin)

    set_target_properties(test_toy_piano_core test_toy_piano_classic_plugin
        PROPERTIES FOLDER "Toy Piano Classic")
endif()
