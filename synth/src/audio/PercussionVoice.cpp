#include "PercussionVoice.h"

namespace
{
/// Per-character decay times, in seconds. Each value is the single source for
/// *both* the audible body contour and the envelope's decay stage (the latter
/// scaled by kEnvelopeDecayRatio), so the two can never drift apart.
constexpr float kKickDecay  = 0.32f;
constexpr float kSnareDecay = 0.18f;
constexpr float kHatDecay   = 0.055f;

/// How much longer the envelope's decay runs than the body contour's.
constexpr float kEnvelopeDecayRatio = 1.8f;

/// Percussive attack. Deliberately tiny rather than exactly zero: it is
/// perceptually instantaneous (~22 samples at 44.1 kHz), which is what a drum
/// transient wants, but it still ramps rather than jumping. That matters because
/// Envelope::noteOn() intentionally does not zero the current level, so a voice
/// stolen mid-hit would otherwise step discontinuously and click.
constexpr float kPercussionAttack = 0.0005f;

/// Only reachable through the panic path: isOneShot() makes noteOff() a no-op,
/// so a drum normally finishes via its decay stage, never via release.
constexpr float kPercussionRelease = 0.02f;
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
    // from the note and stamp so a given sequence still renders deterministically
    // for the self-test.
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

    // Chosen per character below, then used to derive *both* the body contour and
    // the envelope decay -- the latter scaled deliberately longer by
    // kEnvelopeDecayRatio.
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

    // ---- The percussive envelope: why this voice sets its own ----------------
    //
    // usesGlobalEnvelope() returns false, so Voice::setParams() never pushes the
    // UI's ADSR here. That alone is not enough: without setting *something*, the
    // envelope would keep Envelope::Settings' defaults, whose sustain is 0.70.
    // The stage machine would then run Attack -> Decay -> Sustain and park at
    // 0.70 forever, because isOneShot() makes noteOff() a no-op and nothing else
    // would ever start a release. The hit would fall silent (mBodyLevel decays
    // away) while isActive() stayed true, permanently burning a slot in the pool.
    Envelope::Settings percussive;

    percussive.mAttack  = kPercussionAttack;
    percussive.mDecay   = bodyDecaySeconds * kEnvelopeDecayRatio;
    percussive.mSustain = 0.0f;
    percussive.mRelease = kPercussionRelease;

    mEnvelope.setSettings(percussive);
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
