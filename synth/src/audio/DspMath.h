#pragma once

#include <cmath>

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

} // namespace dsp
