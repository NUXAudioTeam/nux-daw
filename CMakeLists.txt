cmake_minimum_required(VERSION 3.22)

project(NUX
    VERSION 0.3.0
    DESCRIPTION "NUX DAW - a digital audio workstation for Windows"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------------------------
# JUCE
#
# JUCE is vendored as a git submodule at external/JUCE (pinned to tag 9.0.5).
# If the submodule is missing, fetch it automatically so a fresh clone can build.
# ------------------------------------------------------------------------------
set(NUX_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")

if (NOT EXISTS "${NUX_JUCE_DIR}/CMakeLists.txt")
    message(STATUS "JUCE submodule not found, fetching JUCE 9.0.5")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
else()
    add_subdirectory("${NUX_JUCE_DIR}" EXCLUDE_FROM_ALL)
endif()

# ------------------------------------------------------------------------------
# Application target
# ------------------------------------------------------------------------------
juce_add_gui_app(NUX
    PRODUCT_NAME        "NUX DAW"
    COMPANY_NAME        "NUX Audio Works"
    BUNDLE_ID           "com.nuxaudio.nuxdaw"
    VERSION             "0.3.0"
    DOCUMENT_EXTENSIONS "nux"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE)

target_sources(NUX PRIVATE
    src/nux_ui/Main.cpp
    src/nux_ui/MainComponent.cpp
    src/nux_ui/TimelineView.cpp
    src/nux_ui/MixerView.cpp
    src/nux_ui/PianoRollView.cpp
    src/nux_ui/PluginPanel.cpp
    src/nux_ui/PluginPicker.cpp
    src/nux_ui/InsertEditorPanel.cpp
    src/nux_ui/AudioSettingsPanel.cpp
    src/nux_ui/TransportBar.cpp
    src/nux_ui/Widgets.cpp
    src/nux_ui/Theme.cpp
    src/nux_engine/AudioClipPlayer.cpp
    src/nux_engine/AudioClipStore.cpp
    src/nux_engine/InputRecorder.cpp
    src/nux_engine/AudioDeviceProbe.cpp
    src/nux_engine/Measurements.cpp
    src/nux_engine/TestSignals.cpp
    src/nux_engine/RoundTrip.cpp
    src/nux_engine/DeviceSelection.cpp
    src/nux_engine/VirtualLoopback.cpp
    src/nux_engine/AudioEngine.cpp
    src/nux_engine/ChannelStrip.cpp
    src/nux_engine/Synth.cpp
    src/nux_engine/PluginManager.cpp
    src/nux_engine/PluginSlot.cpp
    src/nux_engine/PluginRetirementQueue.cpp
    src/nux_core/ProjectDocument.cpp
    src/nux_core/ProjectSerializer.cpp
    src/nux_core/InsertParameters.cpp
    src/nux_core/UndoHistory.cpp
    src/nux_core/TrackModel.cpp)

target_include_directories(NUX PRIVATE src)

target_compile_definitions(NUX PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:NUX,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:NUX,JUCE_VERSION>"
    # Plugin hosting: VST3 only (the SDK ships with JUCE).
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_WINRT_MIDI=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    # Audio backends. ASIO is compiled in on Windows only, where JUCE ships the SDK
    # header it needs; on other platforms the flag is off so the build stays clean.
    JUCE_WASAPI=1
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
    $<$<PLATFORM_ID:Windows>:JUCE_WASAPI_EXCLUSIVE=1>
    $<$<PLATFORM_ID:Windows>:JUCE_DIRECTSOUND=1>)

target_link_libraries(NUX PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(NUX PRIVATE /MP /permissive-)
    set_property(TARGET NUX PROPERTY VS_STARTUP_PROJECT NUX)
else()
    target_compile_options(NUX PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# On Windows, request the WASAPI/ASIO backends and a modern common controls manifest.
if (JUCE_WINDOWS)
    target_link_libraries(NUX PRIVATE)

    # VST3 hosting requires the platform plugin host libraries that JUCE manages.
    juce_add_binary_data(NUXAssets NONE)

    set_target_properties(NUX PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:NUX>")
endif()

# ------------------------------------------------------------------------------
# Tests (headless, no GUI dependencies in the core model and DSP layer)
# ------------------------------------------------------------------------------
option(NUX_BUILD_TESTS "Build the NUX unit tests" ON)

if (NUX_BUILD_TESTS)
    # Without enable_testing() at the top level, ctest finds no tests at all and
    # reports "No tests were found!!!" - which CI treated as a pass, so the unit
    # tests had never actually been run by the workflow.
    enable_testing()
    add_subdirectory(tests)
endif()
