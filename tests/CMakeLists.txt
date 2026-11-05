juce_add_console_app(NUXTests PRODUCT_NAME "NUXTests")

target_sources(NUXTests PRIVATE
    TestMain.cpp
    ProjectModelTests.cpp
    SerializerTests.cpp
    DspTests.cpp
    EngineRenderTests.cpp
    ../src/nux_core/ProjectSerializer.cpp
    ../src/nux_core/TrackModel.cpp
    ../src/nux_engine/AudioEngine.cpp
    ../src/nux_engine/ChannelStrip.cpp
    ../src/nux_engine/Synth.cpp)

target_include_directories(NUXTests PRIVATE ../src)

target_compile_definitions(NUXTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_UNIT_TESTS=1
    JUCE_MODAL_LOOPS_PERMITTED=1)

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

add_test(NAME NUXTests COMMAND NUXTests)
