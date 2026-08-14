#pragma once

#include "DspMath.h"

/// Classic ADSR envelope generator.
class Envelope
{
  public:
    enum class Stage
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    struct Settings
    {
        float mAttack  = 0.01f; // seconds
        float mDecay   = 0.20f; // seconds
        float mSustain = 0.70f; // level, 0..1
        float mRelease = 0.30f; // seconds

        /// Exact float comparison is precisely what is wanted here: the only
        /// question being asked is "did the caller hand us a different value than
        /// we already hold", so that setSettings() can skip recomputing
        /// coefficients that would come out identical.
        bool operator==(const Settings& aOther) const
        {
            return mAttack == aOther.mAttack && mDecay == aOther.mDecay && mSustain == aOther.mSustain &&
                   mRelease == aOther.mRelease;
        }

        bool operator!=(const Settings& aOther) const
        {
            return !(*this == aOther);
        }
    };

    void            prepare(float aSampleRate);
    void            setSettings(const Settings& aSettings);
    const Settings& settings() const
    {
        return mSettings;
    }

    /// Starts (or restarts) the envelope from the current level, so retriggering
    /// a still-sounding voice ramps rather than jumping to zero.
    void noteOn();

    /// Enters the release stage. Safe to call repeatedly.
    void noteOff();

    /// Immediately silences the envelope without a release tail.
    void reset();

    /// Advances one sample and returns the new gain.
    float process();

    float level() const
    {
        return mLevel;
    }

    Stage stage() const
    {
        return mStage;
    }

    /// True once a release has run to completion (or before the first noteOn).
    bool isFinished() const
    {
        return mStage == Stage::Idle;
    }

    bool isReleasing() const
    {
        return mStage == Stage::Release;
    }

  private:
    void refreshRates();

    Settings mSettings;
    Stage    mStage      = Stage::Idle;
    float    mSampleRate = 44100.0f;
    float    mLevel      = 0.0f;

    // Per-sample increments, recomputed only when settings change.
    float mAttackRate  = 1.0f;
    float mDecayRate   = 1.0f;
    float mReleaseRate = 1.0f;
};
