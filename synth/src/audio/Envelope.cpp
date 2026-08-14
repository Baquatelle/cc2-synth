#include "Envelope.h"

void Envelope::prepare(float aSampleRate)
{
    mSampleRate = (aSampleRate > 0.0f) ? aSampleRate : 44100.0f;
    refreshRates();
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
    refreshRates();
}

void Envelope::refreshRates()
{
    // Every stage is a linear ramp, so the per-sample step is simply the full
    // span divided by the number of samples the stage should take. A rate of
    // 1.0f means "instant", which is what a zero time should do.
    mAttackRate  = (mSettings.mAttack > 0.0f) ? (1.0f / (mSettings.mAttack * mSampleRate)) : 1.0f;
    mDecayRate   = (mSettings.mDecay > 0.0f) ? (1.0f / (mSettings.mDecay * mSampleRate)) : 1.0f;
    mReleaseRate = (mSettings.mRelease > 0.0f) ? (1.0f / (mSettings.mRelease * mSampleRate)) : 1.0f;
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
        mLevel -= mDecayRate;
        if (mLevel <= mSettings.mSustain)
        {
            mLevel = mSettings.mSustain;
            if (mLevel <= 0.0f)
            {
                // A zero sustain means the note dies at the end of the decay
                // ramp, so there is nothing left to hold.
                mLevel = 0.0f;
                mStage = Stage::Idle;
            }
            else
            {
                mStage = Stage::Sustain;
            }
        }
        break;

    case Stage::Sustain:
        mLevel = mSettings.mSustain;
        break;

    case Stage::Release:
        mLevel -= mReleaseRate;
        if (mLevel <= 0.0f)
        {
            mLevel = 0.0f;
            mStage = Stage::Idle;
        }
        break;
    }

    return mLevel;
}
