#pragma once

#include <cstddef>
#include <string>
#include <vector>

/// One decoded, immutable one-shot recording.
///
/// This is the *shared* resource in the project's aggregation relationship: a
/// Sample is created and owned by the SampleLibrary and handed to SamplerVoices
/// as a `shared_ptr<const Sample>`. Any number of voices may play the same Sample
/// at once, and the Sample outlives all of them.
struct Sample
{
    std::string mName;

    /// Mono PCM in roughly [-1, 1]. Stereo sources are downmixed at load time so
    /// the voices only ever deal with one channel.
    std::vector<float> mFrames;

    /// Rate the file was recorded at. Playback compensates when it differs from
    /// the audio device rate.
    float mSampleRate = 44100.0f;

    /// MIDI note the recording sounds at, so SamplerVoice knows how far to
    /// transpose. Parsed from an optional `_n<midi>` filename suffix, otherwise
    /// the default below.
    float mBaseMidiNote = 60.0f;

    std::size_t frameCount() const
    {
        return mFrames.size();
    }

    bool empty() const
    {
        return mFrames.empty();
    }

    float durationSeconds() const
    {
        return (mSampleRate > 0.0f) ? static_cast<float>(mFrames.size()) / mSampleRate : 0.0f;
    }

    /// Linearly interpolated read at a fractional frame position.
    /// Out-of-range positions return silence, which lets the caller advance
    /// past the end without a bounds check of its own.
    float readInterpolated(double aPosition) const
    {
        if (mFrames.empty() || aPosition < 0.0)
        {
            return 0.0f;
        }

        const std::size_t index = static_cast<std::size_t>(aPosition);
        if (index + 1 >= mFrames.size())
        {
            return (index < mFrames.size()) ? mFrames[index] : 0.0f;
        }

        const float fraction = static_cast<float>(aPosition - static_cast<double>(index));
        return mFrames[index] + (mFrames[index + 1] - mFrames[index]) * fraction;
    }
};
