#include "StatusPanel.h"

#include "../audio/SampleLibrary.h"
#include "../audio/SynthEngine.h"
#include "Sequencer.h"

#include "ofGraphics.h"

#include <sstream>

void StatusPanel::attach(const SynthEngine* aEngine, const Sequencer* aSequencer, const SampleLibrary* aLibrary)
{
    mEngine    = aEngine;
    mSequencer = aSequencer;
    mLibrary   = aLibrary;
}

void StatusPanel::setAudioInfo(const std::string& aDescription)
{
    mAudioInfo = aDescription;
}

void StatusPanel::draw(float aX, float aY) const
{
    if (mEngine == nullptr)
    {
        return;
    }

    const Envelope::Settings envelope = mEngine->envelopeSettings();

    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);

    text << "AUDIO   " << mAudioInfo << "\n";
    text << "VOICE   " << voiceTypeName(mActiveVoiceType) << "   octave " << (mOctaveOffset >= 0 ? "+" : "")
         << mOctaveOffset << "\n";
    text << "ACTIVE  " << mEngine->activeVoiceCount() << " / " << (SynthEngine::kVoicesPerType * kVoiceTypeCount)
         << " voices\n";
    text << "FILTER  cutoff " << mEngine->cutoff() << " Hz   resonance " << mEngine->resonance() << "\n";
    text << "MORPH   " << mEngine->morph() << "   volume " << mEngine->masterVolume() << "\n";
    text << "ADSR    a " << envelope.mAttack << "  d " << envelope.mDecay << "  s " << envelope.mSustain << "  r "
         << envelope.mRelease << "\n";

    if (mSequencer != nullptr)
    {
        text << "SEQ     " << (mSequencer->isPlaying() ? "playing" : "stopped") << "   " << mSequencer->tempo()
             << " bpm   step " << (mSequencer->currentStep() + 1) << "/" << Sequencer::kSteps << "\n";
    }

    if (mLibrary != nullptr)
    {
        text << "SAMPLES " << mLibrary->size() << " loaded";
        if (!mLibrary->empty())
        {
            const auto sample = mLibrary->at(mEngine->samplerSampleIndex());
            if (sample)
            {
                text << "   selected: " << sample->mName;
            }
        }
        text << "\n";
    }

    // A non-zero drop count means the UI thread outran the audio thread's ability
    // to consume events -- worth surfacing rather than hiding.
    if (mEngine->droppedEvents() > 0)
    {
        text << "WARNING dropped " << mEngine->droppedEvents() << " note events\n";
    }

    ofSetColor(210);
    ofDrawBitmapString(text.str(), aX, aY);
}
