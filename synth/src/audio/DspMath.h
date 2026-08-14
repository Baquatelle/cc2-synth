#pragma once

#include <cmath>

namespace dsp
{

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPi    = 3.14159265358979323846f;

inline float clampf(float aValue, float aLow, float aHigh)
{
    return aValue < aLow ? aLow : (aValue > aHigh ? aHigh : aValue);
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

} // namespace dsp
