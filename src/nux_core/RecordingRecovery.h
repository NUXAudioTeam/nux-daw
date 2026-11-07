/* NUX DAW - recovering recordings that were never finalised.

   A WAV file keeps its length in a header that is written after the audio. That is fine
   until the process dies: the samples are on disk, the header still says "zero samples",
   and every reader - including this application - opens the take as silence. The recording
   is not lost. It is invisible.

   This module reads the actual structure of the file, works out how many samples are really
   there from the file size, and rewrites the two length fields. It is deliberately paranoid:
   it only touches a file whose structure it recognises, it never grows a file, and it says
   what it found instead of guessing. */
#pragma once

#include <juce_core/juce_core.h>

namespace nux
{
/** What a WAV file actually contains, header claims aside. */
struct WavFileInfo
{
    bool ok = false;                // the file could be parsed as RIFF/WAVE
    juce::String error;             // why not, when ok == false

    juce::int64 declaredDataBytes = 0;   // what the header claims
    juce::int64 actualDataBytes = 0;     // what is really there
    bool headerMatchesContents = false;

    int channels = 0;
    int bitsPerSample = 0;
    double sampleRate = 0.0;
    juce::int64 sampleCount = 0;         // after repair, from actualDataBytes

    bool repaired = false;               // repairWav() wrote a corrected header

    // Where the two length fields live, so a caller can reason about the file without
    // re-parsing it. -1 when the file could not be parsed.
    juce::int64 chunkSizeFieldPosition = -1;
    juce::int64 dataPayloadPosition = -1;
};

/** Reads the structure of `file` without changing it. */
WavFileInfo inspectWav (const juce::File& file);

/** Repairs the header when the file holds more audio than the header admits.

    A file that is already consistent is left byte-for-byte alone. A file with no audio in it
    is reported, not invented into a recording. */
WavFileInfo repairWav (const juce::File& file);

/** Takes in `folder` whose headers do not match their contents - recordings that were cut
    short by a crash or a power loss. Missing or empty folders produce an empty list. */
juce::Array<juce::File> findUnfinalisedTakes (const juce::File& folder);
}
