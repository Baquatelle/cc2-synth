#include "Delay.h"

#include "DspMath.h"

#include <algorithm>

void Delay::prepare(float aSampleRate)
{
    mSampleRate = (aSampleRate > 0.0f) ? aSampleRate : 44100.0f;

    // Sized once for the longest settable delay time, so setTimeMs() never
    // has to grow the buffer from the audio thread.
    const std::size_t maxSamples = static_cast<std::size_t>(mSampleRate * kMaxDelayMs / 1000.0f) + 1;
    mBuffer.assign(maxSamples, 0.0f);
    mWriteIndex = 0;
}

void Delay::setTimeMs(float aTimeMs)
{
    mTimeMs = dsp::clampf(aTimeMs, 1.0f, kMaxDelayMs);
}

void Delay::setFeedback(float aFeedback)
{
    mFeedback = dsp::clampf(aFeedback, 0.0f, 0.95f);
}

void Delay::setMix(float aMix)
{
    mMix = dsp::clampf(aMix, 0.0f, 1.0f);
}

void Delay::reset()
{
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mWriteIndex = 0;
}

float Delay::process(float aInput)
{
    if (mBuffer.empty())
    {
        // prepare() was never called -- fail safe by passing audio through
        // unchanged rather than crashing on an empty buffer.
        return aInput;
    }

    const std::size_t bufferSize   = mBuffer.size();
    const std::size_t delaySamples = std::min(bufferSize - 1, static_cast<std::size_t>(mSampleRate * mTimeMs / 1000.0f));
    const std::size_t readIndex    = (mWriteIndex + bufferSize - delaySamples) % bufferSize;

    const float delayed = mBuffer[readIndex];

    // Feed the *input plus a fraction of what's already in the line* back in,
    // which is what makes each repeat quieter than the last by exactly
    // `mFeedback`. Sanitized so one bad upstream sample cannot poison every
    // future echo of it.
    mBuffer[mWriteIndex] = dsp::sanitize(aInput + delayed * mFeedback);
    mWriteIndex           = (mWriteIndex + 1) % bufferSize;

    return aInput * (1.0f - mMix) + delayed * mMix;
}
