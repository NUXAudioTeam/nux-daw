juce_add_console_app(NUXTests PRODUCT_NAME "NUXTests")

target_sources(NUXTests PRIVATE
    TestMain.cpp
    ProjectModelTests.cpp
    SerializerTests.cpp
    DspTests.cpp
    AudioClipTests.cpp
    EngineRenderTests.cpp
    PluginHostTests.cpp
    InsertParameterTests.cpp
    UndoTests.cpp
    RecordingTests.cpp
    AudioDeviceTests.cpp
    OfflineAudioTests.cpp
    DeviceProbeTests.cpp
    MeasurementTests.cpp
    RoundTripTests.cpp
    ../src/nux_core/ProjectDocument.cpp
    ../src/nux_core/ProjectSerializer.cpp
    ../src/nux_core/InsertParameters.cpp
    ../src/nux_core/UndoHistory.cpp
    ../src/nux_core/TrackModel.cpp
    ../src/nux_engine/AudioClipPlayer.cpp
    ../src/nux_engine/AudioClipStore.cpp
    ../src/nux_engine/InputRecorder.cpp
    ../src/nux_engine/AudioDeviceProbe.cpp
    ../src/nux_engine/Measurements.cpp
    ../src/nux_engine/TestSignals.cpp
    ../src/nux_engine/RoundTrip.cpp
    ../src/nux_engine/AudioEngine.cpp
    ../src/nux_engine/ChannelStrip.cpp
    ../src/nux_engine/PluginManager.cpp
    ../src/nux_engine/PluginSlot.cpp
    ../src/nux_engine/PluginRetirementQueue.cpp
    ../src/nux_engine/Synth.cpp)

target_include_directories(NUXTests PRIVATE ../src)

target_compile_definitions(NUXTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_UNIT_TESTS=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    # Must match the application, or the device probe would test a different build.
    JUCE_WASAPI=1
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
    $<$<PLATFORM_ID:Windows>:JUCE_WASAPI_EXCLUSIVE=1>
    $<$<PLATFORM_ID:Windows>:JUCE_DIRECTSOUND=1>)

target_link_libraries(NUXTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_graphics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_data_structures
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# One ctest entry per group, not one for the whole binary.
#
# A single monolithic test is undiagnosable: when the process segfaults, ctest reports
# "NUXTests (SEGFAULT)" and nothing else, which is exactly what happened on Windows CI
# and could not be narrowed down from the log. Registering each group separately makes
# ctest name the group that crashed, on any platform.
set(NUX_TEST_GROUPS
    "project model"
    "project serialization"
    "DSP building blocks"
    "audio clips"
    "engine rendering"
    "plugin hosting"
    "insert parameters"
    "undo history"
    "input recording"
    "audio device"
    "offline audio"
    "device inventory"
    "measurements"
    "audio round trip")

foreach (group IN LISTS NUX_TEST_GROUPS)
    string(REPLACE " " "_" slug "${group}")
    add_test(NAME "NUX_${slug}" COMMAND NUXTests "--only=${group}")

    set_tests_properties("NUX_${slug}" PROPERTIES
        # A group takes a second or two; this stops a hang blocking CI forever.
        TIMEOUT 300
        FAIL_REGULAR_EXPRESSION "FAILED!!")
endforeach()

# The whole suite in one process as well. Two groups can each pass alone and still
# interact (shared global state, a leaked object, a stale device handle), and only a
# single-process run catches that.
add_test(NAME NUXTests COMMAND NUXTests)
set_tests_properties(NUXTests PROPERTIES
    TIMEOUT 900
    FAIL_REGULAR_EXPRESSION "FAILED!!")


# ==============================================================================
# Test plugins: real VST3 binaries built from JUCE, used to verify that NUX can
# host a plugin and that its audio reaches the mix. They are only built when the
# test suite is enabled, so the shipped application never depends on them.
# ==============================================================================
option(NUX_BUILD_TEST_PLUGINS "Build the VST3 test plugins used by the host tests" ON)

if (NUX_BUILD_TEST_PLUGINS)
    juce_add_plugin(NUXTestGain
        COMPANY_NAME "NUX"
        PLUGIN_MANUFACTURER_CODE Nuxu
        PLUGIN_CODE NxGn
        FORMATS VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_NAME "NUX Test Gain"
        PRODUCT_NAME "NUX Test Gain")

    target_sources(NUXTestGain PRIVATE testplugins/GainTestPlugin.cpp)
    target_include_directories(NUXTestGain PRIVATE testplugins)
    target_compile_definitions(NUXTestGain PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    juce_add_plugin(NUXTestTone
        COMPANY_NAME "NUX"
        PLUGIN_MANUFACTURER_CODE Nuxu
        PLUGIN_CODE NxTn
        FORMATS VST3
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_NAME "NUX Test Tone"
        PRODUCT_NAME "NUX Test Tone")

    target_sources(NUXTestTone PRIVATE testplugins/ToneTestInstrument.cpp)
    target_include_directories(NUXTestTone PRIVATE testplugins)
    target_compile_definitions(NUXTestTone PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
endif()
