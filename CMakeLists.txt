cmake_minimum_required(VERSION 3.25)

project(vst3_Plugins
    VERSION 0.1.0
    DESCRIPTION "Experiments and feasibility studies for native VST3 plug-ins"
    LANGUAGES CXX)

include(CTest)

if(NOT WIN32)
    message(FATAL_ERROR "Only Windows x64 is set up so far, see docs/PLANUNG.md (A2).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---------------------------------------------------------------------------
# Steinberg VST3 SDK (git submodule, pinned to a release tag)
# ---------------------------------------------------------------------------
set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk" CACHE PATH
    "Path to a checkout of the Steinberg VST3 SDK")

foreach(part base cmake/modules pluginterfaces public.sdk/source vstgui4/vstgui)
    if(NOT EXISTS "${VST3_SDK_ROOT}/${part}")
        message(FATAL_ERROR
            "The VST3 SDK is incomplete (${part} is missing). Run:\n"
            "  git submodule update --init external/vst3sdk\n"
            "  git -C external/vst3sdk submodule update --init --depth 1 base cmake pluginterfaces public.sdk vstgui4")
    endif()
endforeach()

# The plug-ins need no VC redistributable on the target machine.
set(SMTG_USE_STATIC_CRT ON CACHE BOOL "Use the static MSVC runtime" FORCE)
# Every build ends with Steinberg's validator run on the plug-in.
set(SMTG_RUN_VST_VALIDATOR ON CACHE BOOL "Run the VST3 validator after building a plug-in" FORCE)
set(SMTG_CREATE_MODULE_INFO ON CACHE BOOL "Create moduleinfo.json inside the bundle" FORCE)
# Linking into the system VST3 folder needs administrator rights; install by hand.
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "Link plug-ins into the system VST3 folder" FORCE)

set(SDK_ROOT "${VST3_SDK_ROOT}")
set(public_sdk_SOURCE_DIR "${SDK_ROOT}/public.sdk")
set(pluginterfaces_SOURCE_DIR "${SDK_ROOT}/pluginterfaces")
list(APPEND CMAKE_MODULE_PATH "${SDK_ROOT}/cmake/modules")
include(SMTG_VST3_SDK)

smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()

set(SDK_IDE_LIBS_FOLDER FOLDER "Steinberg VST3 SDK")
set(SDK_IDE_HOSTING_EXAMPLES_FOLDER FOLDER "Steinberg VST3 SDK")
set(SDK_IDE_UTILITIES_FOLDER FOLDER "Steinberg VST3 SDK")
# SYSTEM: warnings inside SDK headers are Steinberg's business, not ours.
include_directories(SYSTEM "${SDK_ROOT}")

# VSTGUI draws the plug-in windows.
smtg_enable_vstgui_support(VSTGUI_SOURCE_DIR "${SDK_ROOT}/vstgui4")
set_target_properties(vstgui vstgui_uidescription vstgui_support PROPERTIES SYSTEM TRUE)

# Only the SDK libraries and the tools the build uses; none of the SDK's examples.
smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
smtg_create_public_sdk_target()
smtg_create_public_sdk_hosting_target()
# The validator must exist before the first plug-in target is declared.
add_subdirectory("${SDK_ROOT}/public.sdk/samples/vst-hosting/validator"
    "${CMAKE_BINARY_DIR}/sdk/validator")
add_subdirectory("${SDK_ROOT}/public.sdk/samples/vst-utilities/moduleinfotool"
    "${CMAKE_BINARY_DIR}/sdk/moduleinfotool")
# A minimal host that opens a plug-in's window: editorhost.exe <path to .vst3>
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON)
add_subdirectory("${SDK_ROOT}/public.sdk/samples/vst-hosting/editorhost"
    "${CMAKE_BINARY_DIR}/sdk/editorhost")

# ---------------------------------------------------------------------------
# Plug-ins
# ---------------------------------------------------------------------------
add_subdirectory(instruments/toy-piano-classic)
add_subdirectory(instruments/torus-classic)
