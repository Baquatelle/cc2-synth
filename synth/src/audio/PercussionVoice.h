#pragma once

#include "Voice.h"

/// Percussion voice: filtered noise burst plus a pitch-swept body tone.
class PercussionVoice : public Voice
{
  public:
    /// Drum flavour, chosen from the incoming note number.
    enum class Character
    {
        Kick,
        Snare,
        Hat
    };

    VoiceType type() const override
    {
        return VoiceType::Percussion;
    }

    /// Drums own their envelope: configureForCharacter() gives each drum in the
    /// kit its own decay with sustain == 0, so the hit terminates by itself.
    bool usesGlobalEnvelope() const override
    {
        return false;
    }

    /// A drum hit always rings for its natural length, like a real drum machine.
    bool isOneShot() const override
    {
        return true;
    }

    Character character() const
    {
        return mCharacter;
    }

  protected:
    void  onPrepare() override;
    void  onNoteOn() override;
    void  onParams(const VoiceParams& aParams) override;
    float generate() override;

  private:
    void configureForCharacter();

    dsp::Noise mNoise;
    Filter     mBodyFilter; // composition: a second owned filter, shaping the noise

    Character mCharacter = Character::Kick;

    float mTonePhase            = 0.0f;
    float mToneFrequency        = 60.0f;
    float mToneTargetFrequency  = 45.0f;
    float mPitchDropCoefficient = 0.0f;

    /// Balance between the noise and the tone components, per character.
    float mNoiseMix = 0.5f;

    /// The audible amplitude contour of the hit, applied on top of this voice's
    /// own percussive envelope and giving the sharp transient a drum needs.
    float mBodyLevel            = 1.0f;
    float mBodyDecayCoefficient = 0.0f;
    float mMorph                = 0.5f;
};
