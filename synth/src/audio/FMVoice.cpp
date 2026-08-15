#include "FMVoice.h"

namespace
{
/// Modulation index range. Above ~8 the spectrum gets so dense it just sounds
/// like noise, so the morph control stops short of that.
constexpr float kMinIndex = 0.5f;
constexpr float kMaxIndex = 8.0f;

/// How long the modulation index takes to fall to its sustain level.
constexpr float kIndexDecaySeconds = 0.45f;

/// Fraction of the peak index that remains once the transient has passed.
constexpr float kIndexSustainRatio = 0.25f;
} // namespace

void FMVoice::onPrepare()
{
    mIndexDecay = dsp::onePoleCoefficient(kIndexDecaySeconds, sampleRate());
    // FM already controls its own brightness through the index; the voice filter
    // sits mostly out of the way and only tames the very top end.
    mFilter.setMode(Filter::Mode::Lowpass);
}

void FMVoice::onNoteOn()
{
    mCarrierPhase   = 0.0f;
    mModulatorPhase = 0.0f;

    // Start at full brightness so the attack has its characteristic clang.
    mIndexCurrent = mIndexTarget;
}

void FMVoice::onParams(const VoiceParams& aParams)
{
    // The XY pad's Y axis maps to modulation depth -- the single most audible
    // control in FM synthesis.
    mIndexTarget = kMinIndex + (kMaxIndex - kMinIndex) * dsp::clampf(aParams.mMorph, 0.0f, 1.0f);

    // Sweep the ratio alongside it: low morph gives a clean harmonic octave
    // (2:1), high morph a deliberately inharmonic ratio for a metallic timbre.
    mRatio = 2.0f + 1.5f * dsp::clampf(aParams.mMorph, 0.0f, 1.0f);
}

float FMVoice::generate()
{
    const float carrierIncrement   = frequency() / sampleRate();
    const float modulatorIncrement = (frequency() * mRatio) / sampleRate();

    const float modulator = std::sin(mModulatorPhase * dsp::kTwoPi);

    // Phase modulation (the standard way to implement "FM" in a digital synth):
    // offsetting the carrier's phase by the modulator is equivalent to frequency
    // modulation but cannot accumulate pitch drift.
    const float output = std::sin((mCarrierPhase + modulator * mIndexCurrent) * dsp::kTwoPi);

    mCarrierPhase   = dsp::wrapPhase(mCarrierPhase + carrierIncrement);
    mModulatorPhase = dsp::wrapPhase(mModulatorPhase + modulatorIncrement);

    // Index falls towards a fraction of its peak, brightening the attack only.
    const float indexFloor = mIndexTarget * kIndexSustainRatio;
    mIndexCurrent += (indexFloor - mIndexCurrent) * mIndexDecay;

    return output;
}
