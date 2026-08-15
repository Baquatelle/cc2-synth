#pragma once

#include "DspMath.h"

/// Topology-preserving-transform state variable filter (Zavalishin).
///
/// Owned *by value* by every Voice, alongside its Envelope -- the second half of
/// the composition relationship described in Voice.h.
class Filter
{
  public:
    enum class Mode
    {
        Lowpass,
        Bandpass,
        Highpass
    };

    void prepare(float aSampleRate);
    void reset();

    /// `aCutoffHz` is clamped to a musically useful range below Nyquist;
    /// `aResonance` runs 0..1 and is mapped internally to a bounded Q.
    void setCutoff(float aCutoffHz);
    void setResonance(float aResonance);
    void setMode(Mode aMode);

    float cutoff() const
    {
        return mCutoffHz;
    }

    float resonance() const
    {
        return mResonance;
    }

    Mode mode() const
    {
        return mMode;
    }

    float process(float aInput);

  private:
    void refreshCoefficients();

    /// Highest cutoff that keeps the tan() prewarp in its stable range for the
    /// current sample rate.
    float maxCutoff() const;

    Mode  mMode       = Mode::Lowpass;
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
