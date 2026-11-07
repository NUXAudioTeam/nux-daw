#include "RecordingRecovery.h"

namespace nux
{
namespace
{
constexpr juce::int64 riffHeaderBytes = 12;
constexpr juce::int64 chunkHeaderBytes = 8;

juce::uint32 readU32 (const char* data)
{
    return (juce::uint32) (juce::uint8) data[0]
         | ((juce::uint32) (juce::uint8) data[1] << 8)
         | ((juce::uint32) (juce::uint8) data[2] << 16)
         | ((juce::uint32) (juce::uint8) data[3] << 24);
}

juce::uint16 readU16 (const char* data)
{
    return (juce::uint16) ((juce::uint16) (juce::uint8) data[0]
                         | ((juce::uint16) (juce::uint8) data[1] << 8));
}

void writeU32 (juce::OutputStream& stream, juce::int64 position, juce::uint32 value)
{
    const char bytes[4] = { (char) (value & 0xff),
                            (char) ((value >> 8) & 0xff),
                            (char) ((value >> 16) & 0xff),
                            (char) ((value >> 24) & 0xff) };

    stream.setPosition (position);
    stream.write (bytes, 4);
}

struct ChunkLocation
{
    juce::int64 sizeFieldPosition = -1;   // where the 4-byte size lives
    juce::int64 payloadPosition = -1;
    juce::int64 declaredSize = 0;
};
}

WavFileInfo inspectWav (const juce::File& file)
{
    WavFileInfo info;

    if (! file.existsAsFile())
    {
        info.error = "The file does not exist.";
        return info;
    }

    const auto fileSize = file.getSize();
    auto stream = file.createInputStream();

    if (stream == nullptr)
    {
        info.error = "The file could not be opened.";
        return info;
    }

    char header[riffHeaderBytes] = {};

    if (stream->read (header, riffHeaderBytes) != riffHeaderBytes
        || juce::String (header, 4) != "RIFF" || juce::String (header + 8, 4) != "WAVE")
    {
        info.error = "This is not a WAV file.";
        return info;
    }

    ChunkLocation format, data;
    auto position = riffHeaderBytes;

    while (position + chunkHeaderBytes <= fileSize)
    {
        char chunkHeader[chunkHeaderBytes] = {};

        stream->setPosition (position);

        if (stream->read (chunkHeader, chunkHeaderBytes) != chunkHeaderBytes)
            break;

        const juce::String id (chunkHeader, 4);
        const auto size = (juce::int64) readU32 (chunkHeader + 4);

        if (id == "fmt " && format.sizeFieldPosition < 0)
        {
            format = { position + 4, position + chunkHeaderBytes, size };

            char fmt[16] = {};

            if (size >= 16 && stream->read (fmt, 16) == 16)
            {
                info.channels = (int) readU16 (fmt + 2);
                info.sampleRate = (double) readU32 (fmt + 4);
                info.bitsPerSample = (int) readU16 (fmt + 14);
            }
        }
        else if (id == "data" && data.sizeFieldPosition < 0)
        {
            data = { position + 4, position + chunkHeaderBytes, size };

            // Everything after the data chunk header is audio.
            info.actualDataBytes = juce::jmax ((juce::int64) 0, fileSize - data.payloadPosition);
            break;
        }

        position += chunkHeaderBytes + size + (size % 2);
    }

    if (format.sizeFieldPosition < 0 || data.sizeFieldPosition < 0)
    {
        info.error = "The file has no format or data chunk.";
        return info;
    }

    if (info.channels <= 0 || info.bitsPerSample <= 0 || info.sampleRate <= 0.0)
    {
        info.error = "The file's format chunk is unusable.";
        return info;
    }

    info.declaredDataBytes = data.declaredSize;

    // A declared size larger than the file means a truncated file; a declared size of zero
    // means the header was never finalised. Both are repaired to what is really there.
    const auto blockAlign = juce::jmax (1, info.channels * info.bitsPerSample / 8);
    const auto usableBytes = (info.actualDataBytes / blockAlign) * blockAlign;

    info.headerMatchesContents = data.declaredSize == info.actualDataBytes;
    info.sampleCount = usableBytes / blockAlign;
    info.ok = true;
    info.chunkSizeFieldPosition = data.sizeFieldPosition;
    info.dataPayloadPosition = data.payloadPosition;
    return info;
}

WavFileInfo repairWav (const juce::File& file)
{
    auto info = inspectWav (file);

    if (! info.ok)
        return info;

    if (info.headerMatchesContents)
        return info;   // nothing to do, and nothing is touched

    if (info.actualDataBytes <= 0)
    {
        // A header with no audio after it. Calling that a recording would be a lie; the
        // caller decides what to do with an empty file.
        info.error = "The file contains no audio.";
        return info;
    }

    const auto blockAlign = juce::jmax (1, info.channels * info.bitsPerSample / 8);
    const auto usableBytes = (info.actualDataBytes / blockAlign) * blockAlign;

    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());

    if (stream == nullptr || ! stream->openedOk())
    {
        info.error = "The file could not be opened for writing.";
        return info;
    }

    // data chunk size, then the RIFF size (everything after the first eight bytes).
    writeU32 (*stream, info.chunkSizeFieldPosition, (juce::uint32) usableBytes);
    writeU32 (*stream, 4, (juce::uint32) (info.dataPayloadPosition + usableBytes - 8));

    stream->flush();   // FileOutputStream::flush() reports nothing; the read-back below is the check
    stream.reset();

    auto verified = inspectWav (file);

    if (! verified.ok || ! verified.headerMatchesContents)
    {
        info.error = "The repaired header still does not match the file.";
        return info;
    }

    verified.repaired = true;
    verified.sampleCount = usableBytes / blockAlign;
    return verified;
}

juce::Array<juce::File> findUnfinalisedTakes (const juce::File& folder)
{
    juce::Array<juce::File> found;

    if (! folder.isDirectory())
        return found;

    for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "take-*.wav",
                                                            juce::File::findFiles))
    {
        const auto info = inspectWav (entry.getFile());

        if (info.ok && ! info.headerMatchesContents && info.actualDataBytes > 0)
            found.add (entry.getFile());
    }

    return found;
}
}
