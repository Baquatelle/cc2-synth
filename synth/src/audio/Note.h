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

/// A single message from the UI thread to the audio thread.
struct NoteEvent
{
    enum class Kind : std::uint8_t
    {
        NoteOn,
        NoteOff
    };

    Kind         mKind      = Kind::NoteOn;
    VoiceType    mVoiceType = VoiceType::FM;
    std::uint8_t mMidiNote  = 60;
    float        mVelocity  = 1.0f;

    /// Distinguishes simultaneous notes at the same pitch from different sources
    /// (computer keyboard vs. sequencer row), so one releasing does not steal the
    /// other's voice.
    std::uint8_t mSourceId = 0;

    static NoteEvent noteOn(int aMidiNote, float aVelocity, VoiceType aType, int aSourceId = 0)
    {
        NoteEvent event;
        event.mKind      = Kind::NoteOn;
        event.mVoiceType = aType;
        event.mMidiNote  = static_cast<std::uint8_t>(dsp::clampf(static_cast<float>(aMidiNote), 0.0f, 127.0f));
        event.mVelocity  = dsp::clampf(aVelocity, 0.0f, 1.0f);
        event.mSourceId  = static_cast<std::uint8_t>(aSourceId);
        return event;
    }

    static NoteEvent noteOff(int aMidiNote, VoiceType aType, int aSourceId = 0)
    {
        NoteEvent event;
        event.mKind      = Kind::NoteOff;
        event.mVoiceType = aType;
        event.mMidiNote  = static_cast<std::uint8_t>(dsp::clampf(static_cast<float>(aMidiNote), 0.0f, 127.0f));
        event.mVelocity  = 0.0f;
        event.mSourceId  = static_cast<std::uint8_t>(aSourceId);
        return event;
    }
};
