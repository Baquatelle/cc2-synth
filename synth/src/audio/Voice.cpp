#include "Voice.h"

void Voice::prepare(float aSampleRate)
{
    mSampleRate = (aSampleRate > 0.0f) ? aSampleRate : 44100.0f;

    // The owned parts are prepared here, by the whole. Nothing outside this class
    // can reach them (composition).
    mEnvelope.prepare(mSampleRate);
    mFilter.prepare(mSampleRate);

    onPrepare();
}

void Voice::noteOn(std::uint8_t aMidiNote, float aVelocity, std::uint8_t aSourceId, std::uint64_t aStamp)
{
    mMidiNote  = aMidiNote;
    mSourceId  = aSourceId;
    mStamp     = aStamp;
    mVelocity  = dsp::clampf(aVelocity, 0.0f, 1.0f);
    mFrequency = notes::midiToFrequency(static_cast<float>(aMidiNote));

    onNoteOn();

    // Retrigger last, so a subclass's onNoteOn() cannot leave the envelope idle
    // and produce a voice the pool believes is free while it is sounding.
    mEnvelope.noteOn();
}

void Voice::noteOff()
{
    // One-shot voices (drums, sampled snippets) play to their natural length.
    // A sequencer gate is far shorter than a cymbal tail, so honouring note-off
    // here would chop every hit off mid-decay.
    if (isOneShot())
    {
        return;
    }

    mEnvelope.noteOff();
}

void Voice::reset()
{
    mEnvelope.reset();
    mFilter.reset();
}

void Voice::fastRelease()
{
    // Nothing to do for a voice the pool already considers free.
    if (mEnvelope.isFinished())
    {
        return;
    }

    // Shorten the *existing* settings rather than constructing fresh ones.
    Envelope::Settings panic = mParams.mEnvelope;
    panic.mRelease           = dsp::kPanicReleaseSeconds;
    mEnvelope.setSettings(panic);

    // Call the envelope directly, deliberately bypassing Voice::noteOff(), which
    // returns early for one-shot voices. A panic has to silence drums too.
    mEnvelope.noteOff();
}

void Voice::setParams(const VoiceParams& aParams)
{
    mParams = aParams;

    // Only sustained instruments follow the global ADSR. Percussive one-shots
    // choose their own envelope at note-on and must not have it overwritten by
    // this per-block push, or their zero sustain would become the UI's non-zero
    // sustain and they would never finish.
    if (usesGlobalEnvelope())
    {
        mEnvelope.setSettings(aParams.mEnvelope);
    }

    mFilter.setCutoff(aParams.mCutoffHz);
    mFilter.setResonance(aParams.mResonance);
    onParams(aParams);
}

float Voice::render()
{
    // Skip the whole chain when idle: with a 48-voice pool this is the difference
    // between a constant filter cost and near-zero cost at rest.
    if (mEnvelope.isFinished())
    {
        return 0.0f;
    }

    // ---- Template Method: the fixed signal chain for every voice type. ----
    const float raw    = generate();           // subclass-specific waveform
    const float shaped = mFilter.process(raw); // owned filter (composition)
    const float gain   = mEnvelope.process();  // owned envelope (composition)

    return shaped * gain * mVelocity;
}
