#include "AudioEngine.h"

namespace nux
{
namespace
{
constexpr int voicesPerTrack = 16;

/** How long a triggered note sounds, as a fraction of one beat. */
constexpr double noteLengthInBeats = 0.45;
}

//==============================================================================
AudioEngine::AudioEngine()
{
    for (auto& track : tracks)
        track.synth.addSound (new NuxSynthSound());

    diskThread.startThread (juce::Thread::Priority::normal);
    startTimerHz (20);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    shutdownAudio();
    stopRecordingToFile();
    diskThread.stopThread (2000);

    if (pendingRetirement != nullptr)
    {
        delete pendingRetirement;
        pendingRetirement = nullptr;
    }
}

//==============================================================================
juce::String AudioEngine::initialiseAudio()
{
    deviceManager.addAudioCallback (this);
    deviceManager.addMidiInputDeviceCallback ({}, this);

    lastDeviceError = deviceManager.initialise (0, 2, nullptr, true);
    audioReady = deviceManager.getCurrentAudioDevice() != nullptr;

    if (! audioReady && lastDeviceError.isEmpty())
        lastDeviceError = "No audio output device is available.";

    if (audioReady)
        if (auto* device = deviceManager.getCurrentAudioDevice())
            prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());

    return lastDeviceError;
}

void AudioEngine::shutdownAudio()
{
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    audioReady = false;
    graphPrepared = false;
}

juce::String AudioEngine::getAudioStatusMessage() const
{
    if (! audioReady)
        return lastDeviceError.isNotEmpty() ? lastDeviceError : "No audio device open";

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto setup = deviceManager.getAudioDeviceSetup();

        return device->getTypeName() + " | " + device->getName()
             + "  " + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz"
             + "  " + juce::String (setup.bufferSize) + " smp";
    }

    return "Audio device open";
}

void AudioEngine::applyPreferredDeviceSetup()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    auto setup = deviceManager.getAudioDeviceSetup();
    const auto rates = device->getAvailableSampleRates();

    if (! rates.isEmpty())
        setup.sampleRate = rates[rates.size() - 1];   // highest supported rate

    const auto sizes = device->getAvailableBufferSizes();

    if (! sizes.isEmpty())
    {
        // Prefer 256 samples, otherwise the closest available size.
        setup.bufferSize = sizes[0];

        for (auto size : sizes)
            if (std::abs (size - 256) < std::abs (setup.bufferSize - 256))
                setup.bufferSize = size;
    }

    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isEmpty())
    {
        audioReady = true;
        lastDeviceError.clear();
    }
    else
    {
        lastDeviceError = error;
    }
}

//==============================================================================
void AudioEngine::setProjectState (ProjectState* state)
{
    projectState = state;
    rebuildGraph();
}

void AudioEngine::rebuildGraph()
{
    if (projectState == nullptr)
        return;

    trackCount = juce::jmin ((int) projectState->tracks.size(), constants::maxTracks);

    for (int i = 0; i < constants::maxTracks; ++i)
    {
        auto& track = tracks[(size_t) i];
        track.synth.clearVoices();

        if (i >= trackCount)
            continue;

        for (int v = 0; v < voicesPerTrack; ++v)
            track.synth.addVoice (new SynthVoice (synthParameters));

        track.synth.setCurrentPlaybackSampleRate (sampleRate);

        const auto& source = projectState->tracks[(size_t) i];
        trackBusIndex[(size_t) i] = juce::jlimit (0, constants::numMixerBuses - 1, source.busIndex);
        tracksSoloed[(size_t) i].store (source.soloed);
        track.strip.getParameters().loadFrom (source);

        if (graphPrepared)
        {
            track.buffer.setSize (2, blockSize, false, true, true);
            track.clipBuffer.setSize (2, blockSize, false, true, true);
            track.strip.prepare (sampleRate, blockSize, 2);
            track.clipPlayer.prepare (sampleRate, blockSize);
        }
    }

    for (int i = 0; i < constants::numMixerBuses; ++i)
    {
        const auto& source = projectState->buses[(size_t) i];
        auto& params = buses[(size_t) i].getParameters();
        params.volumeDb.store (source.volumeDb);
        params.pan.store (source.pan);
        params.muted.store (source.muted);

        for (int s = 0; s < constants::maxBusInserts; ++s)
        {
            params.inserts[(size_t) s] = source.inserts[(size_t) s];
            params.insertTypes[(size_t) s].store ((int) source.inserts[(size_t) s].type, std::memory_order_release);
        }

        if (graphPrepared)
            buses[(size_t) i].prepareBus (sampleRate, blockSize);
    }

    masterBus.getParameters().volumeDb.store (projectState->master.volumeDb);
    masterBus.setLimiterEnabled (projectState->master.limiterEnabled);

    for (int s = 0; s < constants::maxTrackInserts; ++s)
    {
        masterBus.getParameters().inserts[(size_t) s] = projectState->master.inserts[(size_t) s];
        masterBus.getParameters().insertTypes[(size_t) s].store ((int) projectState->master.inserts[(size_t) s].type,
                                                                std::memory_order_release);
    }

    if (graphPrepared)
        masterBus.prepareMaster (sampleRate, blockSize);

    transport.setLoopLengthSteps (projectState->patternLengthSteps);
    transport.setTempo (projectState->tempo);

    // Dotted-eighth delay, locked to the project tempo.
    const auto delaySamplesValue = (60.0 / juce::jmax (20.0, projectState->tempo)) * 0.75 * sampleRate;

    for (auto& bus : buses)
        bus.setDelayTimeSamples (delaySamplesValue);

    applyBusSendLevels();
    syncSequencerFromState();
}

void AudioEngine::syncMixerFromState()
{
    if (projectState == nullptr)
        return;

    const auto count = juce::jmin (trackCount, (int) projectState->tracks.size());

    for (int i = 0; i < count; ++i)
    {
        const auto& source = projectState->tracks[(size_t) i];
        auto& params = tracks[(size_t) i].strip.getParameters();
        params.volumeDb.store (source.volumeDb);
        params.pan.store (source.pan);
        params.muted.store (source.muted);

        for (int s = 0; s < constants::maxTrackInserts; ++s)
        {
            params.inserts[(size_t) s] = source.inserts[(size_t) s];
            params.insertTypes[(size_t) s].store ((int) source.inserts[(size_t) s].type, std::memory_order_release);
        }

        trackBusIndex[(size_t) i] = juce::jlimit (0, constants::numMixerBuses - 1, source.busIndex);
        tracksSoloed[(size_t) i].store (source.soloed);
    }

    for (int i = 0; i < constants::numMixerBuses; ++i)
    {
        const auto& source = projectState->buses[(size_t) i];
        auto& params = buses[(size_t) i].getParameters();
        params.volumeDb.store (source.volumeDb);
        params.pan.store (source.pan);
        params.muted.store (source.muted);

        for (int s = 0; s < constants::maxBusInserts; ++s)
        {
            params.inserts[(size_t) s] = source.inserts[(size_t) s];
            params.insertTypes[(size_t) s].store ((int) source.inserts[(size_t) s].type, std::memory_order_release);
        }
    }

    masterBus.getParameters().volumeDb.store (projectState->master.volumeDb);
    masterBus.setLimiterEnabled (projectState->master.limiterEnabled);

    for (int s = 0; s < constants::maxTrackInserts; ++s)
    {
        masterBus.getParameters().inserts[(size_t) s] = projectState->master.inserts[(size_t) s];
        masterBus.getParameters().insertTypes[(size_t) s].store ((int) projectState->master.inserts[(size_t) s].type,
                                                                std::memory_order_release);
    }
}

void AudioEngine::syncSequencerFromState()
{
    if (projectState == nullptr)
        return;

    // Fill the inactive copy, then publish it atomically.
    auto& target = sequenceSets[(size_t) writeSequenceSet];

    const auto count = juce::jmin (trackCount, (int) projectState->tracks.size());

    for (int i = 0; i < count; ++i)
    {
        const auto& track = projectState->tracks[(size_t) i];
        auto& sequence = target[(size_t) i];

        sequence.isSampler = track.type == TrackType::sampler;
        sequence.rootNote  = track.rootNote;
        sequence.numSteps  = juce::jlimit (constants::minNumSteps, constants::maxNumSteps, track.numSteps);

        for (int s = 0; s < constants::maxNumSteps; ++s)
            sequence.steps[(size_t) s] = track.steps[(size_t) s];
    }

    activeSequenceSet.store (writeSequenceSet, std::memory_order_release);
    writeSequenceSet = 1 - writeSequenceSet;
}

//==============================================================================
// Audio clips
//==============================================================================
void AudioEngine::setMediaRootDirectory (const juce::File& projectFolder)
{
    mediaRoot = projectFolder;
}

juce::File AudioEngine::getMediaRootDirectory() const
{
    return mediaRoot;
}

juce::File AudioEngine::resolveClipFile (const juce::String& clipPath) const
{
    if (clipPath.isEmpty())
        return {};

    if (juce::File::isAbsolutePath (clipPath))
        return juce::File (clipPath);   // absolute paths are used as-is

    const auto root = mediaRoot == juce::File()
                        ? juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                              .getChildFile ("NUX Projects")
                        : mediaRoot;

    // Clip paths are stored relative to the project folder, e.g. "media/kick.wav".
    // File objects must not be built from a relative string, hence getChildFile.
    return root.getChildFile (clipPath);
}

void AudioEngine::reloadAudioClips()
{
    if (projectState == nullptr)
        return;

    const auto count = juce::jmin (trackCount, (int) projectState->tracks.size());

    for (int i = 0; i < constants::maxTracks; ++i)
        clipSnapshots[(size_t) i].clear();

    for (int i = 0; i < count; ++i)
    {
        const auto& track = projectState->tracks[(size_t) i];

        for (const auto& clip : track.audioClips)
        {
            const auto file = resolveClipFile (clip.sourceFile);

            if (! file.existsAsFile())
                continue;

            juce::String error;
            auto buffer = clipStore.load (file, error);

            if (buffer == nullptr)
                continue;

            ClipSnapshot snapshot;
            snapshot.buffer = std::move (buffer);
            snapshot.startStep = clip.startStep;
            snapshot.lengthSteps = clip.lengthSteps;
            snapshot.sourceStartSample = clip.sourceStartSample;
            snapshot.gain = 1.0f;

            clipSnapshots[(size_t) i].push_back (std::move (snapshot));
        }
    }

    for (int i = 0; i < constants::maxTracks; ++i)
        tracks[(size_t) i].clipPlayer.setClips (clipSnapshots[(size_t) i]);
}

double AudioEngine::getClipLengthSeconds (const juce::String& clipPath) const
{
    const auto file = resolveClipFile (clipPath);

    if (auto buffer = clipStore.find (file.getFullPathName()))
        return buffer->getLengthSeconds();

    juce::String error;

    if (auto loaded = const_cast<AudioClipStore&> (clipStore).load (file, error))
        return loaded->getLengthSeconds();

    return 0.0;
}

juce::String AudioEngine::importAudioFile (const juce::File& sourceFile, juce::String& error)
{
    error.clear();

    if (! sourceFile.existsAsFile())
    {
        error = "The file does not exist: " + sourceFile.getFullPathName();
        return {};
    }

    juce::String loadError;

    if (clipStore.load (sourceFile, loadError) == nullptr)
    {
        error = loadError;
        return {};
    }

    const auto mediaFolder = (mediaRoot == juce::File()
                                  ? juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                        .getChildFile ("NUX Projects")
                                  : mediaRoot)
                                .getChildFile ("media");

    if (! mediaFolder.isDirectory() && ! mediaFolder.createDirectory().wasOk())
    {
        error = "Could not create the media folder: " + mediaFolder.getFullPathName();
        return {};
    }

    auto target = mediaFolder.getChildFile (sourceFile.getFileName());

    // Never overwrite media that is already in the project.
    int suffix = 1;

    while (target.existsAsFile() && target.getSize() != sourceFile.getSize())
    {
        target = mediaFolder.getChildFile (sourceFile.getFileNameWithoutExtension()
                                            + "_" + juce::String (++suffix)
                                            + sourceFile.getFileExtension());
    }

    if (! target.existsAsFile())
    {
        if (! sourceFile.copyFileTo (target))
        {
            error = "Could not copy the file into the project media folder.";
            return {};
        }
    }

    return "media/" + target.getFileName();
}

void AudioEngine::applyBusSendLevels()
{
    for (int i = 0; i < constants::numMixerBuses; ++i)
        buses[(size_t) i].setSendLevels (busDelaySends[(size_t) i].load (std::memory_order_relaxed),
                                         busReverbSends[(size_t) i].load (std::memory_order_relaxed));
}

int AudioEngine::getTrackBus (int trackIndex) const
{
    if (! juce::isPositiveAndBelow (trackIndex, trackCount))
        return 0;

    return trackBusIndex[(size_t) trackIndex];
}

//==============================================================================
void AudioEngine::prepare (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    blockSize  = juce::jmax (32, newBlockSize);

    for (int i = 0; i < constants::maxTracks; ++i)
    {
        auto& track = tracks[(size_t) i];
        track.buffer.setSize (2, blockSize, false, true, true);
        track.clipBuffer.setSize (2, blockSize, false, true, true);
        track.strip.prepare (sampleRate, blockSize, 2);
        track.clipPlayer.prepare (sampleRate, blockSize);
        track.synth.setCurrentPlaybackSampleRate (sampleRate);
        sequencerMidi[(size_t) i].ensureSize (256);
        previewMidi[(size_t) i].ensureSize (256);

        noteReleaseCountdown[(size_t) i].fill (0);
        noteHeld[(size_t) i].fill (false);
    }

    for (auto& bus : buses)
        bus.prepareBus (sampleRate, blockSize);

    masterBus.prepareMaster (sampleRate, blockSize);
    recordScratch.setSize (2, blockSize, false, true, true);

    scopeDecimation = juce::jmax (1, (int) std::round (sampleRate / 22050.0));
    graphPrepared = true;

    if (projectState != nullptr)
        rebuildGraph();
}

void AudioEngine::prepareForOfflineRendering (double newSampleRate, int newBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);
    audioReady = false;
    prepare (newSampleRate, newBlockSize);
}

void AudioEngine::renderBlockForTest (juce::AudioBuffer<float>& output, int numSamples)
{
    renderBlock (output, numSamples);
}

void AudioEngine::runBlocksForTest (int numBlocks)
{
    if (! graphPrepared)
        return;

    juce::AudioBuffer<float> scratch (2, blockSize);

    for (int i = 0; i < numBlocks; ++i)
    {
        scratch.clear();
        renderBlock (scratch, blockSize);
    }
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr)
        prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceStopped()
{
    graphPrepared = false;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                   float* const* outputChannelData,
                                                   int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    if (numOutputChannels <= 0 || numSamples <= 0 || outputChannelData == nullptr)
        return;

    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    if (outputChannelData[0] == nullptr)
        return;

    juce::AudioBuffer<float> output (outputChannelData, numOutputChannels, numSamples);
    renderBlock (output, numSamples);

    if (auto* writer = recordingWriter.load (std::memory_order_acquire))
    {
        if (recordScratch.getNumSamples() < numSamples)
            return;

        for (int ch = 0; ch < 2; ++ch)
            recordScratch.copyFrom (ch, 0, output, juce::jmin (ch, output.getNumChannels() - 1), 0, numSamples);

        writer->write (recordScratch.getArrayOfReadPointers(), numSamples);
        recordedSamples.fetch_add (numSamples, std::memory_order_relaxed);
    }
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (projectState == nullptr || ! message.isNoteOn())
        return;

    for (int i = 0; i < trackCount; ++i)
    {
        if (projectState->tracks[(size_t) i].recordArmed)
        {
            triggerPreviewNote (i, message.getNoteNumber(), message.getFloatVelocity());
            return;
        }
    }
}

//==============================================================================
void AudioEngine::noteOn (int trackIndex, int midiNote, float velocity, int sampleOffset)
{
    if (! juce::isPositiveAndBelow (trackIndex, trackCount) || ! juce::isPositiveAndBelow (midiNote, 128))
        return;

    auto& midi = sequencerMidi[(size_t) trackIndex];
    midi.addEvent (juce::MidiMessage::noteOn (1, midiNote, juce::jlimit (0.05f, 1.0f, velocity)),
                   juce::jmax (0, sampleOffset));

    const auto beats = 60.0 / juce::jmax (1.0, transport.getTempo());
    noteReleaseCountdown[(size_t) trackIndex][(size_t) midiNote] =
        (int) juce::jmax (64.0, beats * noteLengthInBeats * sampleRate);
    noteHeld[(size_t) trackIndex][(size_t) midiNote] = true;
}

void AudioEngine::processNoteReleases (int numSamples)
{
    const auto count = juce::jmin (trackCount, constants::maxTracks);

    for (int i = 0; i < count; ++i)
    {
        for (int note = 0; note < 128; ++note)
        {
            if (! noteHeld[(size_t) i][(size_t) note])
                continue;

            auto& countdown = noteReleaseCountdown[(size_t) i][(size_t) note];
            countdown -= numSamples;

            if (countdown <= 0)
            {
                sequencerMidi[(size_t) i].addEvent (juce::MidiMessage::noteOff (1, note), 0);
                noteHeld[(size_t) i][(size_t) note] = false;
                countdown = 0;
            }
        }
    }
}

void AudioEngine::runSequencer (int numSamples)
{
    if (! transport.isPlaying())
        return;

    const auto& sequences = sequenceSets[(size_t) activeSequenceSet.load (std::memory_order_acquire)];

    const auto stepsPerSampleValue = transport.stepsPerSample (sampleRate);
    const auto loopLength = (double) transport.getLoopLengthSteps();
    const auto blockStart = transport.getPositionSteps();
    const auto blockEnd = blockStart + stepsPerSampleValue * numSamples;

    auto stepIndex = (juce::int64) std::floor (blockStart);

    // When playback starts exactly on a boundary, fire that step immediately.
    if (blockStart - (double) stepIndex < 1.0e-9)
        stepIndex = (juce::int64) std::floor (blockStart);

    for (;; ++stepIndex)
    {
        auto scheduledStep = stepIndex;

        if (transport.isLoopEnabled() && loopLength > 0.0)
        {
            while ((double) scheduledStep >= loopLength)
                scheduledStep -= (juce::int64) loopLength;
        }

        auto scheduledPosition = (double) stepIndex;

        if (transport.isLoopEnabled() && loopLength > 0.0)
        {
            // The first iteration may be the step the transport already sits on.
            if (scheduledPosition > blockStart + 1.0e-9)
                scheduledPosition = blockStart + (double) stepIndex - std::floor (blockStart);

            if (scheduledPosition - blockStart > stepsPerSampleValue * (double) numSamples + 1.0e-9)
                break;
        }
        else if (stepIndex > blockEnd)
        {
            break;
        }

        const auto offset = (int) juce::jlimit (0.0, (double) numSamples - 1.0,
                                                (scheduledPosition - blockStart) / juce::jmax (1.0e-12, stepsPerSampleValue));

        const auto step = (int) scheduledStep;

        for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        {
            const auto& sequence = sequences[(size_t) trackIndex];

            if (! sequence.isSampler || ! juce::isPositiveAndBelow (step, sequence.numSteps))
                continue;

            const auto& stepData = sequence.steps[(size_t) step];

            if (stepData.enabled)
                noteOn (trackIndex, sequence.rootNote, stepData.velocity, offset);
        }

        if (stepIndex - std::floor (blockStart) > (double) numSamples + 8.0)
            break;
    }

    auto newPosition = blockEnd;

    if (transport.isLoopEnabled() && loopLength > 0.0 && newPosition >= loopLength)
        newPosition = std::fmod (newPosition, loopLength);

    transport.setPositionSteps (newPosition);
}

//==============================================================================
void AudioEngine::renderBlock (juce::AudioBuffer<float>& output, int numSamples)
{
    output.clear();

    if (! graphPrepared)
        return;

    const auto anySolo = [this]
    {
        for (int i = 0; i < trackCount; ++i)
            if (tracksSoloed[(size_t) i].load (std::memory_order_relaxed))
                return true;
        return false;
    }();

    for (auto& midi : sequencerMidi) midi.clear();
    for (auto& midi : previewMidi)   midi.clear();

    runSequencer (numSamples);
    processNoteReleases (numSamples);

    masterBus.beginBlock (numSamples);

    for (auto& bus : buses)
        bus.beginBlock (numSamples);

    for (int i = 0; i < trackCount; ++i)
    {
        auto& track = tracks[(size_t) i];
        auto& buffer = track.buffer;

        buffer.clear (0, numSamples);

        // Audio clips are mixed in first; the synth adds on top.
        if (! clipSnapshots[(size_t) i].empty())
        {
            auto& clipBuf = track.clipBuffer;
            clipBuf.clear (0, numSamples);

            track.clipPlayer.render (clipBuf, numSamples,
                                     transport.getPositionSteps(),
                                     transport.stepsPerSample (sampleRate),
                                     transport.getLoopLengthSteps(),
                                     transport.isLoopEnabled() && transport.isPlaying());

            for (int ch = 0; ch < juce::jmin (2, clipBuf.getNumChannels()); ++ch)
                buffer.addFrom (ch, 0, clipBuf, ch, 0, numSamples);
        }

        auto& midi = sequencerMidi[(size_t) i];
        midi.swapWith (previewMidi[(size_t) i]);
        midi.addEvents (previewMidi[(size_t) i], 0, -1, 0);

        track.synth.renderNextBlock (buffer, midi, 0, numSamples);

        const auto soloed = tracksSoloed[(size_t) i].load (std::memory_order_relaxed);
        const auto audible = anySolo ? soloed : true;

        if (! audible)
        {
            buffer.clear();
            continue;
        }

        track.strip.process (buffer, 1.0f);

        const auto busIndex = juce::jlimit (0, constants::numMixerBuses - 1, trackBusIndex[(size_t) i]);
        buses[(size_t) busIndex].addFromTrack (buffer);
    }

    for (auto& bus : buses)
        bus.processBus (masterBus);

    masterBus.renderMaster (output, nullptr);
    updateScope (output, numSamples);
}

void AudioEngine::updateScope (const juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto pos = scopeWritePos.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; i += scopeDecimation)
    {
        scopeBuffer[(size_t) (pos % scopeSize)].store (buffer.getSample (0, i), std::memory_order_relaxed);
        ++pos;
    }

    scopeWritePos.store (pos, std::memory_order_relaxed);
}

//==============================================================================
void AudioEngine::timerCallback()
{
    if (pendingRetirement != nullptr && recordingWriter.load (std::memory_order_acquire) == nullptr)
    {
        delete pendingRetirement;
        pendingRetirement = nullptr;
    }
}

//==============================================================================
void AudioEngine::play()
{
    if (! transport.isPlaying())
    {
        panic();
        transport.setPlaying (true);
    }
}

void AudioEngine::stop()
{
    transport.setPlaying (false);
    transport.setRecording (false);
    panic();
}

void AudioEngine::togglePlay()
{
    if (transport.isPlaying())
        stop();
    else
        play();
}

void AudioEngine::panic()
{
    for (int i = 0; i < constants::maxTracks; ++i)
    {
        tracks[(size_t) i].synth.allNotesOff (0, false);
        sequencerMidi[(size_t) i].clear();
        previewMidi[(size_t) i].clear();
        noteHeld[(size_t) i].fill (false);
        noteReleaseCountdown[(size_t) i].fill (0);
    }

    masterBus.reset();
    for (auto& bus : buses) bus.reset();
}

void AudioEngine::startRecording()
{
    transport.setRecording (true);
    transport.setPlaying (true);
}

void AudioEngine::stopRecording()
{
    transport.setRecording (false);
}

void AudioEngine::triggerPreviewNote (int trackIndex, int midiNote, float velocity)
{
    if (! juce::isPositiveAndBelow (trackIndex, trackCount))
        return;

    // Preview notes are queued for the next block; the audio callback owns them.
    previewMidi[(size_t) trackIndex].addEvent (
        juce::MidiMessage::noteOn (1, midiNote, juce::jlimit (0.05f, 1.0f, velocity)), 0);

    const auto beats = 60.0 / juce::jmax (1.0, transport.getTempo());
    noteReleaseCountdown[(size_t) trackIndex][(size_t) juce::jlimit (0, 127, midiNote)] =
        (int) juce::jmax (64.0, beats * noteLengthInBeats * sampleRate);
    noteHeld[(size_t) trackIndex][(size_t) juce::jlimit (0, 127, midiNote)] = true;
}

//==============================================================================
float AudioEngine::getTrackPeak (int trackIndex, int channel) const
{
    if (! juce::isPositiveAndBelow (trackIndex, trackCount))
        return 0.0f;

    return tracks[(size_t) trackIndex].strip.getPeakLevel (channel);
}

float AudioEngine::getMasterPeak (int channel) const
{
    return masterBus.getPeakLevel (channel);
}

float AudioEngine::getLimiterReduction() const
{
    return masterBus.getPeakReduction();
}

int AudioEngine::readScope (float* destination, int maxSamples) const
{
    if (destination == nullptr || maxSamples <= 0)
        return 0;

    const auto count = juce::jmin (maxSamples, scopeSize);
    const auto end = scopeWritePos.load (std::memory_order_relaxed);

    for (int i = 0; i < count; ++i)
    {
        const auto index = (end - count + i + scopeSize * 4) % scopeSize;
        destination[i] = scopeBuffer[(size_t) index].load (std::memory_order_relaxed);
    }

    return count;
}

//==============================================================================
bool AudioEngine::startRecordingToFile (const juce::File& file, juce::String& error)
{
    if (recordingWriter.load() != nullptr)
    {
        error = "Already recording to disk.";
        return false;
    }

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());

    if (stream == nullptr || ! stream->openedOk())
    {
        error = "Could not open the recording file: " + file.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    recordSampleRate = sampleRate;

    auto* fileWriter = wav.createWriterFor (stream.get(), recordSampleRate, 2, 24, {}, 0);

    if (fileWriter == nullptr)
    {
        error = "Could not create a WAV writer for the recording.";
        return false;
    }

    stream.release();   // the writer owns the stream from here on

    recordedSamples.store (0);
    recordingWriter.store (new WriterType (fileWriter, diskThread, 32768), std::memory_order_release);
    return true;
}

void AudioEngine::stopRecordingToFile()
{
    auto* writer = recordingWriter.exchange (nullptr, std::memory_order_acq_rel);

    if (writer != nullptr)
    {
        if (pendingRetirement != nullptr)
            delete pendingRetirement;

        pendingRetirement = writer;
    }
}

double AudioEngine::getRecordingLengthSeconds() const
{
    return recordSampleRate > 0.0 ? (double) recordedSamples.load() / recordSampleRate : 0.0;
}

//==============================================================================
bool AudioEngine::exportToWav (const ExportOptions& options, juce::String& error,
                              const std::function<void (double)>& progress)
{
    if (projectState == nullptr)
    {
        error = "No project is open.";
        return false;
    }

    if (! graphPrepared)
    {
        error = "The audio engine is not prepared. Open an audio device first.";
        return false;
    }

    if (renderingOffline.load())
    {
        error = "An export is already running.";
        return false;
    }

    options.targetFile.getParentDirectory().createDirectory();
    options.targetFile.deleteFile();

    auto stream = std::unique_ptr<juce::FileOutputStream> (options.targetFile.createOutputStream());

    if (stream == nullptr || ! stream->openedOk())
    {
        error = "Could not write to: " + options.targetFile.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));

    if (writer == nullptr)
    {
        error = "Could not create the WAV writer.";
        return false;
    }

    stream.release();

    // Pause the live transport, render from step 0, then restore it.
    const auto wasPlaying = transport.isPlaying();
    const auto previousPosition = transport.getPositionSteps();
    const auto previousLooping = transport.isLoopEnabled();

    transport.setPlaying (false);
    transport.setLoopEnabled (false);   // render straight through, no repeats in the tail
    panic();
    renderingOffline.store (true);

    const auto stepsPerSecond = (transport.getTempo() / 60.0) * constants::stepsPerBeat;
    const auto songSamples  = (juce::int64) std::ceil (((double) options.lengthSteps / stepsPerSecond) * sampleRate);
    const auto tailSamples  = (juce::int64) (juce::jmax (0.1, options.tailSeconds) * sampleRate);
    const auto totalSamples = songSamples + tailSamples;

    juce::AudioBuffer<float> renderBuffer (2, blockSize);
    juce::int64 written = 0;
    auto success = true;

    transport.setPositionSteps (0.0);
    transport.setPlaying (true);

    while (written < totalSamples)
    {
        const auto samples = (int) juce::jmin ((juce::int64) blockSize, totalSamples - written);

        renderBuffer.clear();
        renderBlock (renderBuffer, samples);

        if (written >= songSamples)
        {
            const auto tailPosition = (float) (written - songSamples) / (float) juce::jmax ((juce::int64) 1, tailSamples);
            renderBuffer.applyGain (juce::jlimit (0.0f, 1.0f, 1.0f - tailPosition));
        }

        if (! writer->writeFromAudioSampleBuffer (renderBuffer, 0, samples))
        {
            error = "The disk write failed while rendering. Check free space.";
            success = false;
            break;
        }

        written += samples;

        if (progress)
            progress ((double) written / (double) juce::jmax ((juce::int64) 1, totalSamples));
    }

    writer.reset();

    transport.setPlaying (false);
    panic();
    transport.setPositionSteps (0.0);
    transport.setLoopEnabled (previousLooping);
    renderingOffline.store (false);

    if (success)
    {
        transport.setPositionSteps (previousPosition);
        if (wasPlaying)
            transport.setPlaying (true);
    }

    return success;
}
}
