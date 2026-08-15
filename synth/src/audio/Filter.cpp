#include "Filter.h"

namespace
{
constexpr float kMinCutoff = 20.0f;

/// Highest resonance feedback we allow. k = 2 is the neutral (Q = 0.5) case and
/// k -> 0 is self-oscillation; stopping short of zero keeps the filter from
/// ringing forever when the XY pad is slammed into the corner.
constexpr float kMinFeedback = 0.10f;
} // namespace

void Filter::prepare(float aSampleRate)
{
    mSampleRate = (aSampleRate > 0.0f) ? aSampleRate : 44100.0f;

    // Re-clamp the existing cutoff against the *new* rate before deriving
    // coefficients.
    mCutoffHz = dsp::clampf(mCutoffHz, kMinCutoff, maxCutoff());

    refreshCoefficients();
    reset();
}

void Filter::reset()
{
    mIc1eq = 0.0f;
    mIc2eq = 0.0f;
}

float Filter::maxCutoff() const
{
    // Keep well below Nyquist: the tan() prewarp blows up as cutoff approaches it.
    return mSampleRate * 0.45f;
}

void Filter::setCutoff(float aCutoffHz)
{
    const float clamped = dsp::clampf(aCutoffHz, kMinCutoff, maxCutoff());
    if (clamped != mCutoffHz)
    {
        mCutoffHz = clamped;
        refreshCoefficients();
    }
}

void Filter::setResonance(float aResonance)
{
    const float clamped = dsp::clampf(aResonance, 0.0f, 1.0f);
    if (clamped != mResonance)
    {
        mResonance = clamped;
        refreshCoefficients();
    }
}

void Filter::setMode(Mode aMode)
{
    mMode = aMode;
}

void Filter::refreshCoefficients()
{
    mG = std::tan(dsp::kPi * mCutoffHz / mSampleRate);
    mK = 2.0f - (2.0f - kMinFeedback) * mResonance;

    mA1 = 1.0f / (1.0f + mG * (mG + mK));
    mA2 = mG * mA1;
    mA3 = mG * mA2;
}

float Filter::process(float aInput)
{
    const float v3 = aInput - mIc2eq;
    const float v1 = mA1 * mIc1eq + mA2 * v3;
    const float v2 = mIc2eq + mA2 * mIc1eq + mA3 * v3;

    mIc1eq = dsp::flushDenormal(2.0f * v1 - mIc1eq);
    mIc2eq = dsp::flushDenormal(2.0f * v2 - mIc2eq);

    switch (mMode)
    {
    case Mode::Bandpass:
        return v1;
    case Mode::Highpass:
        return aInput - mK * v1 - v2;
    case Mode::Lowpass:
    default:
        return v2;
    }
}
