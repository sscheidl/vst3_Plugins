# Toy Piano Generative: the Toy Piano sound engine played by a generative step sequencer.

# Its own version; moduleinfo.json takes it from here. Keep it equal to
# FULL_VERSION_STR in source/version.h.
project(ToyPianoGenerative VERSION 0.1.0 LANGUAGES CXX)

# The sequencer on its own: plain C++, no SDK, no plug-in types. /fp:precise
# because the SDK's /fp:fast would let the compiler assume away the NaN check
# in the knob law.
add_library(generative_sequencer STATIC
    sequencer/generative_sequencer.cpp
    sequencer/generative_sequencer.h)
target_include_directories(generative_sequencer PUBLIC sequencer)
target_compile_options(generative_sequencer PRIVATE /W4 /permissive- /fp:precise)
set_target_properties(generative_sequencer PROPERTIES FOLDER "Toy Piano Generative")

if(BUILD_TESTING)
    add_executable(test_generative_sequencer tests/test_sequencer.cpp)
    target_compile_options(test_generative_sequencer PRIVATE /W4 /permissive- /fp:precise)
    target_link_libraries(test_generative_sequencer PRIVATE generative_sequencer)
    add_test(NAME toy-piano-generative.sequencer COMMAND test_generative_sequencer)
    set_target_properties(test_generative_sequencer PROPERTIES FOLDER "Toy Piano Generative")
endif()
