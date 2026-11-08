/* NUX DAW - tests for recovering recordings that were never finalised.

   The scenario these exist for: the machine dies mid-take. The audio is on disk, the WAV
   header still says "zero samples", and opening the file gives silence. Nothing here needs
   a crash to test - a 4-byte edit to the header is exactly the same file a crash leaves. */
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/nux_core/RecordingRecovery.h"

#include <cmath>
#include <cstring>

namespace nux
{
class RecordingRecoveryTests final : public juce::UnitTest
{
public:
    RecordingRecoveryTests() : juce::UnitTest ("NUX recording recovery", "NUX") {}

    static juce::File makeTempFolder()
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nux-recording-recovery")
                          .getChildFile (juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
        folder.createDirectory();
        return folder;
    }

    /** Writes a real WAV through JUCE, so the file under test is a genuine file. */
    static juce::File writeWav (const juce::File& file, int numSamples, double rate = 44100.0)
    {
        file.deleteFile();

        auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));

        if (writer == nullptr)
            return {};

        stream.release();

        juce::AudioBuffer<float> buffer (2, numSamples);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, 0.5f * std::sin ((float) i * 0.01f));

        writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        writer->flush();
        writer.reset();   // finalises the header

        return file;
    }

    static juce::uint32 readU32 (const char* data)
    {
        return (juce::uint32) (juce::uint8) data[0]
             | ((juce::uint32) (juce::uint8) data[1] << 8)
             | ((juce::uint32) (juce::uint8) data[2] << 16)
             | ((juce::uint32) (juce::uint8) data[3] << 24);
    }

    /** Makes a file look like one whose writer never got to write its header: the four-byte
        data size is set to zero while the audio is left in place. This is byte-for-byte what
        a crash during a take leaves on disk. */
    static void zeroDataChunkSize (const juce::File& file)
    {
        juce::MemoryBlock block;
        file.loadFileAsData (block);

        auto* bytes = static_cast<char*> (block.getData());
        size_t position = 12;

        while (position + 8 <= block.getSize())
        {
            const juce::String id (bytes + position, 4);
            const auto size = (size_t) readU32 (bytes + position + 4);

            if (id == "data")
            {
                std::memset (bytes + position + 4, 0, 4);
                file.replaceWithData (block.getData(), block.getSize());
                return;
            }

            position += 8 + size + (size % 2);
        }
    }

    /** Cuts a file short, the way a crash mid-write does. */
    static void truncateFile (const juce::File& file, juce::int64 newSize)
    {
        juce::MemoryBlock block;
        file.loadFileAsData (block);
        block.setSize ((size_t) newSize, true);
        file.replaceWithData (block.getData(), block.getSize());
    }

    static juce::int64 readBack (const juce::File& file)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(),
                                                                            true));
        return reader != nullptr ? reader->lengthInSamples : -1;
    }

    void runTest() override
    {
        const auto folder = makeTempFolder();

        beginTest ("A finished file is reported as consistent and is left alone");
        {
            const auto file = writeWav (folder.getChildFile ("finished.wav"), 1000);
            expect (file.existsAsFile(), "the fixture should have been written");

            const auto sizeBefore = file.getSize();
            const auto info = inspectWav (file);

            expect (info.ok, "a real WAV should parse: " + info.error);
            expect (info.headerMatchesContents, "its header matches its contents");
            expectEquals (info.channels, 2);
            expectEquals (info.sampleRate, 44100.0);
            expectEquals (info.sampleCount, (juce::int64) 1000);

            const auto repaired = repairWav (file);
            expect (repaired.ok, "repairing a good file is a no-op: " + repaired.error);
            expect (! repaired.repaired, "and it is not reported as repaired");
            expectEquals (file.getSize(), sizeBefore, "the file is untouched");
        }

        beginTest ("A take that lost its header is repaired to its real length");
        {
            const auto file = writeWav (folder.getChildFile ("crash.wav"), 5000);
            const auto sizeBefore = file.getSize();
            zeroDataChunkSize (file);

            // Before the repair: exactly what the application would see after a crash.
            expectEquals (readBack (file), (juce::int64) 0,
                          "a file whose header was never written reads as empty");

            const auto inspected = inspectWav (file);
            expect (inspected.ok, inspected.error);
            expect (! inspected.headerMatchesContents, "and the inspector notices");
            expect (inspected.actualDataBytes > 0, "because the audio is really there");

            const auto repaired = repairWav (file);
            expect (repaired.ok, "repair: " + repaired.error);
            expect (repaired.repaired, "the header was rewritten");
            expectEquals (repaired.sampleCount, (juce::int64) 5000);
            expectEquals (repaired.declaredDataBytes, repaired.actualDataBytes,
                          "the header now admits the bytes that are there");

            expectEquals (readBack (file), (juce::int64) 5000,
                          "and the file reads back as the take it is");
            expectEquals (file.getSize(), sizeBefore, "without adding or removing any audio");
        }

        beginTest ("A truncated file is repaired to the samples that survived");
        {
            const auto file = writeWav (folder.getChildFile ("truncated.wav"), 4000);
            const auto fullSize = file.getSize();

            // Lop off the last 900 bytes: half a frame's worth of ragged edge included.
            truncateFile (file, fullSize - 900);

            const auto before = inspectWav (file);
            expect (before.ok, before.error);
            expect (! before.headerMatchesContents, "a truncated file is inconsistent");
            expect (before.actualDataBytes < before.declaredDataBytes,
                    "the header claims more audio than the file holds");

            const auto repaired = repairWav (file);
            expect (repaired.ok, "repair: " + repaired.error);
            expect (repaired.repaired, "the header was corrected downwards");

            const auto readBackSamples = readBack (file);
            expect (readBackSamples > 0, "the surviving audio is still readable");
            expect (readBackSamples < 4000, "and it is fewer samples than were written");
            expectEquals (readBackSamples, repaired.sampleCount,
                          "the repaired length matches what reads back");
        }

        beginTest ("A header with no audio after it is not invented into a recording");
        {
            const auto file = writeWav (folder.getChildFile ("header-only.wav"), 100);
            const auto headerSize = file.getSize() - 100 * 2 * 3;

            truncateFile (file, headerSize);   // the crash landed between header and audio

            const auto info = repairWav (file);
            expect (! info.repaired, "there is nothing to recover");
            expect (info.error.isNotEmpty(), "and the caller is told so");
            expectEquals (info.actualDataBytes, (juce::int64) 0, "there is no audio in the file");
        }

        beginTest ("Only unfinalised takes are listed as recoverable");
        {
            const auto scanFolder = makeTempFolder();

            writeWav (scanFolder.getChildFile ("take-good.wav"), 500);
            const auto broken = writeWav (scanFolder.getChildFile ("take-broken.wav"), 500);
            zeroDataChunkSize (broken);
            writeWav (scanFolder.getChildFile ("other-name.wav"), 500);   // not a take

            const auto found = findUnfinalisedTakes (scanFolder);
            expectEquals (found.size(), 1, "exactly one take needs recovering");

            if (! found.isEmpty())
                expectEquals (found.getFirst().getFileName(), juce::String ("take-broken.wav"));

            expect (findUnfinalisedTakes (scanFolder.getChildFile ("does-not-exist")).isEmpty(),
                    "a missing folder produces an empty list, not a crash");
        }

        beginTest ("A file that is not a WAV is refused");
        {
            const auto file = folder.getChildFile ("not-a-wav.bin");
            file.replaceWithText ("this is not audio");

            const auto info = inspectWav (file);
            expect (! info.ok, "it must not be parsed as audio");
            expect (info.error.isNotEmpty(), "and the reason is given");

            const auto repaired = repairWav (file);
            expect (! repaired.ok, "and it must not be repaired");
        }

        // Leave nothing behind in /tmp.
        folder.getParentDirectory().deleteRecursively();
    }
};

static RecordingRecoveryTests recordingRecoveryTests;
}
