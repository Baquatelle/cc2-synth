#include "Envelope.h"

void Envelope::prepare(float aSampleRate)
{
    mSampleRate = (aSampleRate > 0.0f) ? aSampleRate : 44100.0f;
    refreshCoefficients();
    reset();
}

void Envelope::setSettings(const Settings& aSettings)
{
    Settings clamped = aSettings;
    clamped.mAttack  = dsp::clampf(clamped.mAttack, 0.0f, 10.0f);
    clamped.mDecay   = dsp::clampf(clamped.mDecay, 0.0f, 10.0f);
    clamped.mSustain = dsp::clampf(clamped.mSustain, 0.0f, 1.0f);
    clamped.mRelease = dsp::clampf(clamped.mRelease, 0.0f, 10.0f);

    if (clamped == mSettings)
    {
        return;
    }

    mSettings = clamped;
    refreshCoefficients();
}

void Envelope::refreshCoefficients()
{
    // Attack is linear: it is short enough that the curve shape does not matter,
    // and a linear ramp reaches the peak in exactly the requested time.
    mAttackRate = (mSettings.mAttack > 0.0f) ? (1.0f / (mSettings.mAttack * mSampleRate)) : 1.0f;

    // Decay and release are exponential, which is how natural sounds behave.
    mDecayCoefficient   = dsp::decayCoefficient(mSettings.mDecay, mSampleRate);
    mReleaseCoefficient = dsp::decayCoefficient(mSettings.mRelease, mSampleRate);
}

void Envelope::noteOn()
{
    // Deliberately do NOT zero mLevel: starting the attack from wherever the
    // envelope currently sits means a retriggered or stolen voice ramps from its
    // present amplitude instead of jumping discontinuously (which clicks).
    mStage = Stage::Attack;
}

void Envelope::noteOff()
{
    if (mStage != Stage::Idle)
    {
        mStage = Stage::Release;
    }
}

void Envelope::reset()
{
    mStage = Stage::Idle;
    mLevel = 0.0f;
}

float Envelope::process()
{
    switch (mStage)
    {
    case Stage::Idle:
        mLevel = 0.0f;
        break;

    case Stage::Attack:
        mLevel += mAttackRate;
        if (mLevel >= 1.0f)
        {
            mLevel = 1.0f;
            mStage = Stage::Decay;
        }
        break;

    case Stage::Decay:
        if (mSettings.mSustain <= dsp::kSilence)
        {
            mLevel *= mDecayCoefficient;
            if (mLevel <= dsp::kSilence)
            {
                mLevel = 0.0f;
                mStage = Stage::Idle;
            }
        }
        else
        {
            // Decay exponentially towards the sustain level. Approaching a
            // non-zero asymptote needs the offset form, otherwise the level
            // would keep sliding past the sustain level towards zero.
            mLevel = mSettings.mSustain + (mLevel - mSettings.mSustain) * mDecayCoefficient;
            if (mLevel - mSettings.mSustain <= dsp::kSilence)
            {
                mLevel = mSettings.mSustain;
                mStage = Stage::Sustain;
            }
        }
        break;

    case Stage::Sustain:
        mLevel = mSettings.mSustain;
        break;

    case Stage::Release:
        mLevel *= mReleaseCoefficient;
        if (mLevel <= dsp::kSilence)
        {
            mLevel = 0.0f;
            mStage = Stage::Idle;
        }
        break;
    }

    mLevel = dsp::flushDenormal(mLevel);
    return mLevel;
}
