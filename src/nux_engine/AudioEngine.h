/* NUX DAW - the audio engine.

   Owns the audio device, the per-track synths and channel strips, the mixer
   buses, the master bus, the step sequencer and the recorder / WAV exporter.

   Threading contract
   ------------------
   * The audio callback only touches engine-owned structures (tracks, buses,
     atomics) and the sequencer snapshot. It never allocates.
   * Structural edits (adding tracks, replacing the project, changing insert
     types) are performed on the message thread while the transport is stopped.
   * Note releases are scheduled by a sample counter inside the engine, so no
     note can ever be left hanging because its note-off fell outside a block. */
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <array>
#include <memory>

#include "../nux_core/TrackModel.h"
#include "AudioClipPlayer.h"
#include "AudioClipStore.h"
#include "InputRecorder.h"
#include "TestTone.h"
#include "ChannelStrip.h"
#include "PluginManager.h"
#include "PluginSlot.h"
#include "Synth.h"
#include "Transport.h"

namespace nux
{
/** One engine-side track: a synth, its channel strip and its render buffer. */
struct TrackEngine
{
    juce::Synthesiser synth;
    ChannelStrip strip;
    AudioClipPlayer clipPlayer;
    juce::AudioBuffer<float> buffer;
    juce::AudioBuffer<float> clipBuffer;
};

class AudioEngine final : public juce::AudioIODeviceCallback,
                          public juce::MidiInputCallback,
                          private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    //==============================================================================
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    /** Opens the default audio device. Returns an empty string on success. */
    juce::String initialiseAudio();
    void shutdownAudio();
    bool isAudioAvailable() const noexcept { return audioReady; }
    juce::String getAudioStatusMessage() const;

    /** Asks the current driver for a low latency setup: the highest sample rate it
        supports with a 256 sample buffer, falling back to whatever it will accept. */
    void applyPreferredDeviceSetup();

    /** True when the device disappeared and NUX has not reopened it yet. */
    bool isDeviceLost() const noexcept { return deviceLost.load (std::memory_order_acquire); }

    /** Reopens the audio device after a disconnection. Returns true on success. */
    bool tryReopenDevice();

    /** The last device error, for the UI. */
    juce::String getLastDeviceError() const;

    //==============================================================================
    Transport& getTransport() noexcept { return transport; }
    const Transport& getTransport() const noexcept { return transport; }

    void setProjectState (ProjectState* state);
    ProjectState* getProjectState() const noexcept { return projectState; }

    /** Re-creates synths / strips / buses from the current project state. */
    void rebuildGraph();

    /** Pushes fader, pan, mute, send and insert changes into the running graph. */
    void syncMixerFromState();

    /** Re-reads pattern data (steps) into the sequencer snapshot. */
    void syncSequencerFromState();

    //==============================================================================
    // Hosted plugins (VST3 and anything else JUCE can host)

    /** Which strip a plugin insert belongs to. */
    enum class StripKind { track, bus, master };

    void setPluginManager (PluginManager* manager) noexcept { pluginManager = manager; }
    PluginManager* getPluginManager() const noexcept { return pluginManager; }

    /** Instantiates the plugin described by the current project state for one insert
        slot, restores its saved state and puts it in the strip's signal path.

        The project state is the single source of truth: the caller writes the
        PluginReference (and optional state) into the model first, then calls this.
        Returns an empty string on success, otherwise a message for the user. */
    juce::String loadInsertPlugin (StripKind kind, int stripIndex, int slotIndex);

    /** Instantiates the hosted instrument described by project state for a track,
        restoring its saved state. The built-in sampler is bypassed while it plays. */
    juce::String loadTrackInstrument (int trackIndex);

    void unloadInsertPlugin (StripKind kind, int stripIndex, int slotIndex);
    void unloadTrackInstrument (int trackIndex);

    /** Recreates every plugin referenced by the current project state. Slots whose
        plugin cannot be found are reported through `warnings`. */
    void reloadAllPlugins (juce::StringArray& warnings);

    /** Re-prepares every live plugin instance after a sample rate / block change. */
    void preparePluginsForGraph();

    /** Writes the live state of every hosted plugin back into the project state so
        the next save can store it. Call before serialising. */
    void capturePluginStates();

    /** Number of hosted plugin instances currently alive in the graph. */
    int getNumLoadedPlugins() const;

    /** The live instance for an insert slot, or nullptr. Used by the editor window. */
    PluginSlot* getInsertPluginSlot (StripKind kind, int stripIndex, int slotIndex);

    /** The hosted instrument of a track, or nullptr when the sampler is in use. */
    PluginSlot* getTrackInstrumentSlot (int trackIndex);

    //==============================================================================
    // Audio clips

    /** Folder that relative clip paths are resolved against (the project folder). */
    void setMediaRootDirectory (const juce::File& projectFolder);
    juce::File getMediaRootDirectory() const;
    juce::File resolveClipFile (const juce::String& clipPath) const;

    /** Decodes every clip used by the current project and rebuilds the clip snapshots.
        Call on the message thread after loading a project or importing audio. */
    void reloadAudioClips();

    /** Copies an audio file into the project's media folder, loads it and returns the
        path to store in the project. Empty on failure. */
    juce::String importAudioFile (const juce::File& sourceFile, juce::String& error);

    /** Number of clip files currently decoded. */
    int getNumLoadedClipFiles() const noexcept { return clipStore.getNumLoaded(); }
    double getClipLengthSeconds (const juce::String& clipPath) const;
    juce::int64 getClipMemoryUsageBytes() const noexcept { return clipStore.getMemoryUsageBytes(); }

    //==============================================================================
    float getTrackPeak (int trackIndex, int channel) const;
    float getMasterPeak (int channel) const;
    float getLimiterReduction() const;
    int getNumTracks() const noexcept { return trackCount; }
    double getSampleRateHint() const noexcept { return sampleRate; }
    int getBlockSizeHint() const noexcept { return blockSize; }
    int getTrackBus (int trackIndex) const;

    /** Bus send levels, edited by the mixer's bus strips. */
    std::array<std::atomic<float>, constants::numMixerBuses>& getBusSendLevels() noexcept { return busDelaySends; }
    std::array<std::atomic<float>, constants::numMixerBuses>& getBusReverbSendLevels() noexcept { return busReverbSends; }
    void applyBusSendLevels();

    /** Copies the most recent master output for the UI scope display. */
    int readScope (float* destination, int maxSamples) const;

    /** Auditions a note from a drum pad, the piano roll or an external MIDI keyboard. */
    void triggerPreviewNote (int trackIndex, int midiNote, float velocity);

    //==============================================================================
    // Audio input recording

    /** The track whose armed state decides where the take lands (-1 when none). */
    int getArmedTrack() const;

    /** Starts capturing the audio input into a WAV file inside the project's media
        folder. `mediaFolder` must exist. Returns an empty string on success. */
    juce::String startInputRecording (const juce::File& mediaFolder);

    /** Stops capturing and returns the finished take. Empty name when nothing was
        recorded. The file is left on disk for the caller to place on a track. */
    struct RecordedTake
    {
        juce::File file;
        double startStep = 0.0;
        double lengthSeconds = 0.0;
        int trackIndex = -1;
    };

    RecordedTake stopInputRecording();

    bool isInputRecording() const noexcept { return inputRecorder.isRecording(); }
    float getInputLevel() const noexcept { return inputRecorder.getInputLevel(); }
    double getRecordingSeconds() const noexcept { return inputRecorder.getRecordedSeconds(); }

    /** Flushes captured audio to disk; call from a timer while recording. */
    void pumpInputRecorder() { inputRecorder.drainToFile(); }

    /** True when the device actually has input channels available. */
    bool hasAudioInput() const;

    //==============================================================================
    // Test tone (proves audio reaches the device)

    /** Starts / stops the 440 Hz test tone. The ramp keeps it click-free. */
    void setTestToneEnabled (bool shouldBeOn);
    bool isTestToneEnabled() const noexcept { return testTone.isActive(); }
    void setTestToneLevel (float level) { testTone.setLevel (level); }
    float getTestToneLevel() const noexcept { return testTone.getLevel(); }
    void setTestToneFrequency (double hz) { testTone.setFrequency (hz); }

    /** Output peak of what was actually sent to the device last block. */
    float getOutputPeak (int channel) const noexcept
    {
        return outputPeaks[(size_t) juce::jlimit (0, 1, channel)].load (std::memory_order_relaxed);
    }

    /** Input level reported by the recorder (0 when not recording). */
    float getInputPeak() const noexcept { return inputRecorder.getInputLevel(); }

    //==============================================================================
    void play();
    void stop();
    void togglePlay();
    void startRecording();
    void stopRecording();
    void panic();

    //==============================================================================
    struct ExportOptions
    {
        juce::File targetFile;
        int lengthSteps = constants::defaultNumSteps;
        double tailSeconds = 1.5;
    };

    /** Renders the song offline to a 24-bit WAV. Blocks the caller; call from the
        message thread and show your own progress UI with the callback. */
    bool exportToWav (const ExportOptions& options, juce::String& error,
                      const std::function<void (double)>& progress = {});

    bool startRecordingToFile (const juce::File& file, juce::String& error);
    void stopRecordingToFile();
    bool isRecordingToFile() const noexcept { return recordingWriter.load (std::memory_order_acquire) != nullptr; }
    double getRecordingLengthSeconds() const;

    //==============================================================================
    // Headless helpers, used by the unit tests and by any future offline batch tool.

    /** Prepares the graph without opening an audio device. */
    void prepareForOfflineRendering (double newSampleRate, int newBlockSize, int numChannels);

    /** Renders `numBlocks` blocks through the graph, discarding the output. */
    void runBlocksForTest (int numBlocks);

    /** Renders one block into `output` through the full graph. For diagnostics/tests. */
    void renderBlockForTest (juce::AudioBuffer<float>& output, int numSamples);

    //==============================================================================
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

private:
    ChannelStrip* findStrip (StripKind kind, int stripIndex);
    std::unique_ptr<PluginSlot> createSlotFor (const PluginReference& reference,
                                               bool wantMidi, juce::String& error);
    void stopDeviceForGraphEdit();
    void restartDeviceAfterGraphEdit();
    void prepare (double newSampleRate, int newBlockSize);
    void renderBlock (juce::AudioBuffer<float>& output, int numSamples);
    void runSequencer (int numSamples);
    void processNoteReleases (int numSamples);
    void noteOn (int trackIndex, int midiNote, float velocity, int sampleOffset);
    void updateScope (const juce::AudioBuffer<float>& buffer, int numSamples);
    void timerCallback() override;

    juce::AudioDeviceManager deviceManager;
    Transport transport;

    ProjectState* projectState = nullptr;
    int trackCount = 0;
    bool audioReady = false;
    juce::String lastDeviceError;

    std::array<TrackEngine, constants::maxTracks> tracks;
    std::array<StereoBus, constants::numMixerBuses> buses;
    MasterBus masterBus;
    std::array<int, constants::maxTracks> trackBusIndex {};
    std::array<std::atomic<bool>, constants::maxTracks> tracksSoloed {};
    std::array<std::atomic<float>, constants::numMixerBuses> busDelaySends {};
    std::array<std::atomic<float>, constants::numMixerBuses> busReverbSends {};

    /** MIDI destined for each track during the current block. */
    /** Guards previewMidi: the UI/MIDI threads produce, the audio thread consumes. */
    juce::SpinLock previewMidiLock;

    std::array<juce::MidiBuffer, constants::maxTracks> sequencerMidi;
    std::array<juce::MidiBuffer, constants::maxTracks> previewMidi;

    /** Samples remaining before a note-off is issued, indexed by MIDI note. */
    std::array<std::array<int, 128>, constants::maxTracks> noteReleaseCountdown {};
    std::array<std::array<bool, 128>, constants::maxTracks> noteHeld {};

    SynthParameters synthParameters;

    PluginManager* pluginManager = nullptr;

    AudioClipStore clipStore;
    InputRecorder inputRecorder;
    TestToneGenerator testTone;
    std::array<std::atomic<float>, 2> outputPeaks { { {0.0f}, {0.0f} } };
    std::atomic<int> armedTrackIndex { -1 };
    std::atomic<bool> deviceLost { false };
    juce::File mediaRoot;
    std::array<std::vector<ClipSnapshot>, constants::maxTracks> clipSnapshots;

    double sampleRate = 44100.0;
    int blockSize = 512;
    bool graphPrepared = false;
    std::atomic<bool> renderingOffline { false };

    /** Sequencer snapshot, written on the message thread between engine calls. */
    struct TrackSequence
    {
        bool isSampler = false;
        int rootNote = 36;
        int numSteps = constants::defaultNumSteps;
        std::array<Step, constants::maxNumSteps> steps {};
    };

    /* Double buffered so the audio thread never reads a half-written pattern:
       the message thread fills the inactive copy, then flips the index. */
    using SequenceSet = std::array<TrackSequence, constants::maxTracks>;
    std::array<SequenceSet, 2> sequenceSets {};
    std::atomic<int> activeSequenceSet { 0 };
    int writeSequenceSet = 1;

    /** Recording to disk. */
    juce::TimeSliceThread diskThread { "NUX disk writer" };
    using WriterType = juce::AudioFormatWriter::ThreadedWriter;
    std::atomic<WriterType*> recordingWriter { nullptr };
    WriterType* pendingRetirement = nullptr;
    std::atomic<juce::int64> recordedSamples { 0 };
    double recordSampleRate = 44100.0;
    juce::AudioBuffer<float> recordScratch;

    /** Scope ring buffer for the UI. */
    static constexpr int scopeSize = 1024;
    std::array<std::atomic<float>, scopeSize> scopeBuffer {};
    std::atomic<int> scopeWritePos { 0 };
    int scopeDecimation = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
}
