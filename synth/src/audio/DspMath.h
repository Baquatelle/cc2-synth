#pragma once

#include <cmath>
#include <cstdint>

namespace dsp
{

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPi    = 3.14159265358979323846f;

/// Anything quieter than this is treated as silence.
constexpr float kDenormalFloor = 1.0e-25f;

/// Amplitude at which an envelope is considered finished.
constexpr float kSilence = 1.0e-4f;

/// Release time used by the panic path (all-notes-off).
constexpr float kPanicReleaseSeconds = 0.005f;

inline float clampf(float aValue, float aLow, float aHigh)
{
    return aValue < aLow ? aLow : (aValue > aHigh ? aHigh : aValue);
}

inline float flushDenormal(float aValue)
{
    return (std::fabs(aValue) < kDenormalFloor) ? 0.0f : aValue;
}

/// Replaces NaN/Inf with silence.
inline float sanitize(float aValue)
{
    return std::isfinite(aValue) ? aValue : 0.0f;
}

/// Smooth saturation used as the master limiter. Monotonic and bounded to
/// (-1, 1), so stacked voices compress instead of clipping harshly.
inline float softClip(float aValue)
{
    return std::tanh(aValue);
}

/// Keeps a normalised phase accumulator (one cycle == 1.0) inside [0, 1).
inline float wrapPhase(float aPhase)
{
    while (aPhase >= 1.0f)
    {
        aPhase -= 1.0f;
    }
    return aPhase;
}

/// Converts a one-pole time constant in seconds into a per-sample coefficient
/// that reaches ~99.9% of its target after `aSeconds`.
inline float onePoleCoefficient(float aSeconds, float aSampleRate)
{
    if (aSeconds <= 0.0f || aSampleRate <= 0.0f)
    {
        return 1.0f;
    }
    return clampf(1.0f - std::exp(-6.9077553f / (aSeconds * aSampleRate)), 0.0f, 1.0f);
}

/// Per-sample multiplier that decays 1.0 down to kSilence over `aSeconds`.
/// Returns 0 for a non-positive time so the caller's stage ends immediately.
inline float decayCoefficient(float aSeconds, float aSampleRate)
{
    if (aSeconds <= 0.0f || aSampleRate <= 0.0f)
    {
        return 0.0f;
    }
    // ln(1 / kSilence) == 9.2103404 for kSilence == 1e-4.
    return std::exp(-9.2103404f / (aSeconds * aSampleRate));
}

/// xorshift32 white noise.
class Noise
{
  public:
    explicit Noise(std::uint32_t aSeed = 0x1234567u) : mState(aSeed ? aSeed : 1u)
    {
    }

    void reseed(std::uint32_t aSeed)
    {
        mState = aSeed ? aSeed : 1u;
    }

    /// Uniform white noise in roughly [-1, 1).
    float next()
    {
        mState ^= mState << 13;
        mState ^= mState >> 17;
        mState ^= mState << 5;
        return static_cast<float>(static_cast<std::int32_t>(mState)) * (1.0f / 2147483648.0f);
    }

  private:
    std::uint32_t mState;
};

} // namespace dsp
