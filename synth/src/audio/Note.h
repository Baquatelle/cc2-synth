#pragma once

#include "DspMath.h"

#include <cstdint>

/// Which family of sound generator should play a note.
enum class VoiceType : std::uint8_t
{
    FM = 0,
    Percussion,
    Sampler
};

constexpr int kVoiceTypeCount = 3;

inline const char* voiceTypeName(VoiceType aType)
{
    switch (aType)
    {
    case VoiceType::FM:
        return "FM";
    case VoiceType::Percussion:
        return "Percussion";
    case VoiceType::Sampler:
        return "Sampler";
    }
    return "Unknown";
}

namespace notes
{

/// MIDI note 69 == A4 == 440 Hz.
inline float midiToFrequency(float aMidiNote)
{
    return 440.0f * std::pow(2.0f, (aMidiNote - 69.0f) / 12.0f);
}

} // namespace notes
