#pragma once

#include "DspMath.h"

/// Topology-preserving-transform state variable filter (Zavalishin).
class Filter
{
  public:
    void prepare(float aSampleRate);
    void reset();

    /// `aCutoffHz` is clamped to a musically useful range below Nyquist;
    /// `aResonance` runs 0..1 and is mapped internally to a bounded Q.
    void setCutoff(float aCutoffHz);
    void setResonance(float aResonance);

    float cutoff() const
    {
        return mCutoffHz;
    }

    float resonance() const
    {
        return mResonance;
    }

    float process(float aInput);

  private:
    void refreshCoefficients();

    float mSampleRate = 44100.0f;
    float mCutoffHz   = 8000.0f;
    float mResonance  = 0.2f;

    // Derived coefficients.
    float mG  = 0.0f;
    float mK  = 1.0f;
    float mA1 = 0.0f;
    float mA2 = 0.0f;
    float mA3 = 0.0f;

    // Integrator state.
    float mIc1eq = 0.0f;
    float mIc2eq = 0.0f;
};
