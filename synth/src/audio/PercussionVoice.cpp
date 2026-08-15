#include "PercussionVoice.h"

namespace
{
/// Per-character decay times, in seconds. Each value sets the audible body
/// contour for that drum.
constexpr float kKickDecay  = 0.32f;
constexpr float kSnareDecay = 0.18f;
constexpr float kHatDecay   = 0.055f;
} // namespace

void PercussionVoice::onPrepare()
{
    mBodyFilter.prepare(sampleRate());
    mFilter.setMode(Filter::Mode::Lowpass);
    configureForCharacter();
}

void PercussionVoice::onNoteOn()
{
    // Map the note to a drum: three-note groups repeat up the keyboard, so a
    // sequencer row (or a run of keyboard keys) cycles kick -> snare -> hat.
    switch (midiNote() % 3)
    {
    case 0:
        mCharacter = Character::Kick;
        break;
    case 1:
        mCharacter = Character::Snare;
        break;
    default:
        mCharacter = Character::Hat;
        break;
    }

    configureForCharacter();

    mTonePhase = 0.0f;
    mBodyLevel = 1.0f;
    mBodyFilter.reset();

    // Reseed per hit so repeated hits are not bit-identical, but derive the seed
    // from the note and stamp so a given sequence still renders deterministically.
    mNoise.reseed(static_cast<std::uint32_t>(0x9E3779B9u * (stamp() + 1u) + midiNote()));
}

void PercussionVoice::onParams(const VoiceParams& aParams)
{
    mMorph = dsp::clampf(aParams.mMorph, 0.0f, 1.0f);
    configureForCharacter();
}

void PercussionVoice::configureForCharacter()
{
    const float rate = sampleRate();

    // Chosen per character below, then used to derive the audible body contour.
    float bodyDecaySeconds = kKickDecay;

    switch (mCharacter)
    {
    case Character::Kick:
        // Steep drop from a click into a low thud.
        mToneFrequency        = 150.0f;
        mToneTargetFrequency  = 45.0f;
        mPitchDropCoefficient = dsp::onePoleCoefficient(0.045f, rate);
        mNoiseMix             = 0.06f;
        bodyDecaySeconds      = kKickDecay;
        mBodyFilter.setMode(Filter::Mode::Lowpass);

        // Morph opens the low end up a little for a punchier kick.
        mBodyFilter.setCutoff(400.0f + 2200.0f * mMorph);
        mBodyFilter.setResonance(0.15f);
        break;

    case Character::Snare:
        // Noise-dominated with a short tuned body.
        mToneFrequency        = 210.0f;
        mToneTargetFrequency  = 160.0f;
        mPitchDropCoefficient = dsp::onePoleCoefficient(0.06f, rate);
        mNoiseMix             = 0.72f;
        bodyDecaySeconds      = kSnareDecay;
        mBodyFilter.setMode(Filter::Mode::Bandpass);
        mBodyFilter.setCutoff(1200.0f + 3200.0f * mMorph);
        mBodyFilter.setResonance(0.35f);
        break;

    case Character::Hat:
        // Pure high noise, very short.
        mToneFrequency        = 320.0f;
        mToneTargetFrequency  = 320.0f;
        mPitchDropCoefficient = 0.0f;
        mNoiseMix             = 1.0f;
        bodyDecaySeconds      = kHatDecay;
        mBodyFilter.setMode(Filter::Mode::Highpass);
        mBodyFilter.setCutoff(5000.0f + 4000.0f * mMorph);
        mBodyFilter.setResonance(0.2f);
        break;
    }

    mBodyDecayCoefficient = dsp::onePoleCoefficient(bodyDecaySeconds, rate);
}

float PercussionVoice::generate()
{
    // Pitch sweep: the defining gesture of a synthesised drum.
    if (mPitchDropCoefficient > 0.0f)
    {
        mToneFrequency += (mToneTargetFrequency - mToneFrequency) * mPitchDropCoefficient;
    }

    const float tone        = std::sin(mTonePhase * dsp::kTwoPi);
    mTonePhase              = dsp::wrapPhase(mTonePhase + mToneFrequency / sampleRate());
    const float shapedNoise = mBodyFilter.process(mNoise.next());
    const float mixed       = shapedNoise * mNoiseMix + tone * (1.0f - mNoiseMix);
    const float output      = mixed * mBodyLevel; //< Percussive amplitude contour on top of the shared ADSR.

    mBodyLevel -= mBodyLevel * mBodyDecayCoefficient;
    mBodyLevel = dsp::flushDenormal(mBodyLevel);

    return output;
}
