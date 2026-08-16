#include "SamplerVoice.h"

#include "SampleLibrary.h"

void SamplerVoice::onPrepare()
{
    mFilter.setMode(Filter::Mode::Lowpass);
}

void SamplerVoice::onNoteOn()
{
    mPosition = 0.0;

    if (mLibrary == nullptr)
    {
        mSample.reset();
        mIncrement = 1.0;
        return;
    }

    mSample = mLibrary->at(sampleIndex());

    if (!mSample || mSample->empty())
    {
        mIncrement = 1.0;
        return;
    }

    // Transposition relative to the recording's own pitch, times the correction
    // for a file recorded at a different rate than the device is running at.
    const double pitch = static_cast<double>(notes::pitchRatio(static_cast<float>(midiNote()), mSample->mBaseMidiNote));
    const double rateCorrection = static_cast<double>(mSample->mSampleRate) / static_cast<double>(sampleRate());
    mIncrement                  = pitch * rateCorrection;
}

void SamplerVoice::onParams(const VoiceParams& /*aParams*/)
{
}

float SamplerVoice::generate()
{
    if (!mSample || mSample->empty())
    {
        return 0.0f;
    }

    const float output = mSample->readInterpolated(mPosition);
    mPosition += mIncrement;

    // A one-shot that has run out should not hold its voice: release the envelope
    // so the pool can reclaim it once the tail has finished.
    if (mPosition >= static_cast<double>(mSample->frameCount()))
    {
        mEnvelope.noteOff();
    }

    return output;
}
