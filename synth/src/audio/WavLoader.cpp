#include "WavLoader.h"

#include "DspMath.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace
{

constexpr std::uint16_t kFormatPcm = 1;

/// All multi-byte WAV fields are little-endian. We assemble them byte by byte
/// rather than memcpy-ing into a struct so the parser behaves identically
/// regardless of host endianness, struct padding, or alignment rules.
std::uint16_t readU16(const unsigned char* aData)
{
    return static_cast<std::uint16_t>(aData[0]) | static_cast<std::uint16_t>(static_cast<std::uint16_t>(aData[1]) << 8);
}

std::uint32_t readU32(const unsigned char* aData)
{
    return static_cast<std::uint32_t>(aData[0]) | (static_cast<std::uint32_t>(aData[1]) << 8) |
           (static_cast<std::uint32_t>(aData[2]) << 16) | (static_cast<std::uint32_t>(aData[3]) << 24);
}

} // namespace

namespace wav
{

bool load(const std::string& aPath, Sample& aOutSample, std::string& aOutError)
{
    aOutError.clear();

    std::ifstream file(aPath, std::ios::binary);
    if (!file)
    {
        aOutError = "cannot open file";
        return false;
    }

    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12)
    {
        aOutError = "file too short to be a RIFF container";
        return false;
    }

    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    {
        aOutError = "not a RIFF/WAVE file";
        return false;
    }

    std::uint16_t        format        = 0;
    std::uint16_t        channels      = 0;
    std::uint32_t        sampleRate    = 0;
    std::uint16_t        bitsPerSample = 0;
    const unsigned char* dataChunk     = nullptr;
    std::size_t          dataSize      = 0;
    bool                 haveFormat    = false;

    std::size_t offset = 12;
    while (offset + 8 <= bytes.size())
    {
        const unsigned char* header     = bytes.data() + offset;
        const std::uint32_t  chunkSize  = readU32(header + 4);
        const std::size_t    bodyOffset = offset + 8;

        // Guard against a truncated or malicious size field.
        const std::size_t available = bytes.size() - bodyOffset;
        const std::size_t bodySize  = (chunkSize <= available) ? chunkSize : available;

        if (std::memcmp(header, "fmt ", 4) == 0 && bodySize >= 16)
        {
            const unsigned char* fmt = bytes.data() + bodyOffset;
            format                   = readU16(fmt);
            channels                 = readU16(fmt + 2);
            sampleRate               = readU32(fmt + 4);
            bitsPerSample            = readU16(fmt + 14);

            haveFormat = true;
        }
        else if (std::memcmp(header, "data", 4) == 0)
        {
            dataChunk = bytes.data() + bodyOffset;
            dataSize  = bodySize;
        }

        // Chunks are word-aligned: an odd size is followed by a pad byte.
        offset = bodyOffset + bodySize + (bodySize & 1u);
    }

    if (!haveFormat)
    {
        aOutError = "missing 'fmt ' chunk";
        return false;
    }
    if (dataChunk == nullptr || dataSize == 0)
    {
        aOutError = "missing or empty 'data' chunk";
        return false;
    }
    if (format != kFormatPcm)
    {
        aOutError =
            "unsupported (compressed) format tag " + std::to_string(format) + "; only uncompressed PCM is supported";
        return false;
    }
    if (channels == 0 || sampleRate == 0)
    {
        aOutError = "invalid channel count or sample rate";
        return false;
    }
    if (bitsPerSample != 16)
    {
        aOutError = "unsupported bit depth " + std::to_string(bitsPerSample) + "; only 16-bit PCM is supported";
        return false;
    }

    const std::size_t bytesPerSample = bitsPerSample / 8u;
    const std::size_t bytesPerFrame  = bytesPerSample * channels;
    const std::size_t frameCount     = dataSize / bytesPerFrame;
    if (frameCount == 0)
    {
        aOutError = "no complete frames in 'data' chunk";
        return false;
    }

    // Downmix to mono: the voices only ever deal with one channel, and averaging
    // here keeps that decision out of the audio thread.
    aOutSample.mFrames.assign(frameCount, 0.0f);
    aOutSample.mSampleRate = static_cast<float>(sampleRate);

    const float channelScale = 1.0f / static_cast<float>(channels);
    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
        const unsigned char* framePtr = dataChunk + frame * bytesPerFrame;
        float                sum      = 0.0f;
        for (std::uint16_t channel = 0; channel < channels; ++channel)
        {
            const std::int16_t value = static_cast<std::int16_t>(readU16(framePtr + channel * bytesPerSample));
            sum += static_cast<float>(value) / 32768.0f;
        }
        aOutSample.mFrames[frame] = dsp::clampf(sum * channelScale, -1.0f, 1.0f);
    }

    return true;
}

} // namespace wav
