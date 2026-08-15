#include "SynthEngine.h"

#include "FMVoice.h"

SynthEngine::SynthEngine()  = default;
SynthEngine::~SynthEngine() = default;

void SynthEngine::prepare(float aSampleRate, std::size_t aOutputChannels)
{
    // Called only while the audio stream is stopped -- see the header. This
    // reconstructs the pool, so doing it under a live stream would free voices
    // while the audio thread renders them.
    mSampleRate     = (aSampleRate > 0.0f) ? aSampleRate : 44100.0f;
    mOutputChannels = (aOutputChannels > 0) ? aOutputChannels : 2;

    mVoices.clear();
    mVoices.reserve(kMaxVoices);

    // Allocate the whole pool up front: the audio thread must never allocate, so
    // note-on can only ever pick an already-constructed voice.
    for (std::size_t i = 0; i < kMaxVoices; ++i)
    {
        mVoices.emplace_back(std::make_unique<FMVoice>());
    }

    for (auto& voice : mVoices)
    {
        voice->prepare(mSampleRate);
    }
}

void SynthEngine::noteOn(VoiceType aType, int aMidiNote, float aVelocity, std::uint8_t aSourceId)
{
    mEvents.push(NoteEvent::noteOn(aMidiNote, aVelocity, aType, aSourceId));
}

void SynthEngine::noteOff(VoiceType aType, int aMidiNote, std::uint8_t aSourceId)
{
    mEvents.push(NoteEvent::noteOff(aMidiNote, aType, aSourceId));
}

Voice* SynthEngine::acquireVoice(VoiceType aType)
{
    (void)aType;

    for (std::size_t i = 0; i < mVoices.size(); ++i)
    {
        if (!mVoices[i]->isActive())
        {
            return mVoices[i].get();
        }
    }

    return nullptr;
}

void SynthEngine::handleEvent(const NoteEvent& aEvent)
{
    // Audio thread: it is safe to touch voices directly from here.
    if (aEvent.mKind == NoteEvent::Kind::NoteOn)
    {
        if (Voice* voice = acquireVoice(aEvent.mVoiceType))
        {
            voice->noteOn(aEvent.mMidiNote, aEvent.mVelocity, aEvent.mSourceId, 0);
        }
        return;
    }

    // NoteOff: release every voice matching pitch and source.
    for (std::size_t i = 0; i < mVoices.size(); ++i)
    {
        Voice* voice = mVoices[i].get();
        if (voice->isActive() && voice->midiNote() == aEvent.mMidiNote && voice->sourceId() == aEvent.mSourceId)
        {
            voice->noteOff();
        }
    }
}

void SynthEngine::process(float* aBuffer, std::size_t aNumFrames, std::size_t aNumChannels)
{
    if (!aBuffer || aNumFrames == 0 || aNumChannels == 0)
    {
        return;
    }

    // Drain control input first so notes start at the top of the block.
    NoteEvent event;
    while (mEvents.pop(event))
    {
        handleEvent(event);
    }

    for (std::size_t frame = 0; frame < aNumFrames; ++frame)
    {
        float mix = 0.0f;
        for (auto& voice : mVoices)
        {
            if (voice->isActive())
            {
                mix += voice->render();
            }
        }

        // Scaled down so a handful of simultaneous voices does not clip.
        float out = mix * 0.25f;

        for (std::size_t channel = 0; channel < aNumChannels; ++channel)
        {
            aBuffer[frame * aNumChannels + channel] = out;
        }
    }
}
