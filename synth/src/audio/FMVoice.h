#pragma once

#include "Voice.h"

/// Two-operator FM voice (carrier + modulator).
///
/// One of the two reasons the project cannot use `ofSoundPlayer`: this timbre
/// does not exist as a file, it is computed per sample from the carrier phase and
/// a modulator that the XY pad reshapes live.
class FMVoice : public Voice
{
  public:
    VoiceType type() const override
    {
        return VoiceType::FM;
    }

  protected:
    void  onPrepare() override;
    void  onNoteOn() override;
    void  onParams(const VoiceParams& aParams) override;
    float generate() override;

  private:
    float mCarrierPhase   = 0.0f;
    float mModulatorPhase = 0.0f;

    /// Modulator frequency as a multiple of the carrier. Integer ratios sound
    /// harmonic/bell-like; the morph control leans towards a slightly detuned
    /// ratio for a metallic edge.
    float mRatio = 2.0f;

    /// Peak modulation depth, in carrier cycles.
    float mIndexTarget  = 2.0f;
    float mIndexCurrent = 0.0f;
    float mIndexDecay   = 0.0f;
};
