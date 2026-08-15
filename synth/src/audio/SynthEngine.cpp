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

    applyParamsToPool();

    mNextStamp = 1;
}

void SynthEngine::noteOn(VoiceType aType, int aMidiNote, float aVelocity, std::uint8_t aSourceId)
{
    mEvents.push(NoteEvent::noteOn(aMidiNote, aVelocity, aType, aSourceId));
}

void SynthEngine::noteOff(VoiceType aType, int aMidiNote, std::uint8_t aSourceId)
{
    mEvents.push(NoteEvent::noteOff(aMidiNote, aType, aSourceId));
}

void SynthEngine::allNotesOff()
{
    // Queued, not applied here: releasing voices directly from the UI thread
    // would mutate objects that process() is concurrently rendering.
    mEvents.push(NoteEvent::allNotesOff());
}

void SynthEngine::setCutoff(float aHz)
{
    mCutoff.store(dsp::clampf(aHz, 20.0f, 18000.0f), std::memory_order_relaxed);
}

void SynthEngine::setResonance(float aValue)
{
    mResonance.store(dsp::clampf(aValue, 0.0f, 0.95f), std::memory_order_relaxed);
}

void SynthEngine::setMorph(float aValue)
{
    mMorph.store(dsp::clampf(aValue, 0.0f, 1.0f), std::memory_order_relaxed);
}

void SynthEngine::setEnvelope(const Envelope::Settings& aSettings)
{
    mAttack.store(aSettings.mAttack, std::memory_order_relaxed);
    mDecay.store(aSettings.mDecay, std::memory_order_relaxed);
    mSustain.store(aSettings.mSustain, std::memory_order_relaxed);
    mRelease.store(aSettings.mRelease, std::memory_order_relaxed);
}

Envelope::Settings SynthEngine::envelopeSettings() const
{
    Envelope::Settings settings;
    settings.mAttack  = mAttack.load(std::memory_order_relaxed);
    settings.mDecay   = mDecay.load(std::memory_order_relaxed);
    settings.mSustain = mSustain.load(std::memory_order_relaxed);
    settings.mRelease = mRelease.load(std::memory_order_relaxed);
    return settings;
}

void SynthEngine::applyParamsToPool()
{
    VoiceParams params;
    params.mCutoffHz  = cutoff();
    params.mResonance = resonance();
    params.mMorph     = morph();
    params.mEnvelope  = envelopeSettings();

    for (auto& voice : mVoices)
    {
        voice->setParams(params);
    }
}

Voice* SynthEngine::acquireVoice(VoiceType aType, std::uint64_t& aOutStamp)
{
    (void)aType;

    // Prefer a genuinely free voice.
    for (std::size_t i = 0; i < mVoices.size(); ++i)
    {
        if (!mVoices[i]->isActive())
        {
            aOutStamp = mNextStamp++;
            return mVoices[i].get();
        }
    }

    // Then one that is already releasing (its tail is quietest).
    Voice*        candidate = nullptr;
    std::uint64_t oldest    = ~std::uint64_t(0);

    for (std::size_t i = 0; i < mVoices.size(); ++i)
    {
        if (mVoices[i]->isReleasing() && mVoices[i]->stamp() < oldest)
        {
            oldest    = mVoices[i]->stamp();
            candidate = mVoices[i].get();
        }
    }

    // Finally, steal the oldest sounding voice.
    if (!candidate)
    {
        oldest = ~std::uint64_t(0);
        for (std::size_t i = 0; i < mVoices.size(); ++i)
        {
            if (mVoices[i]->stamp() < oldest)
            {
                oldest    = mVoices[i]->stamp();
                candidate = mVoices[i].get();
            }
        }
    }

    if (candidate)
    {
        aOutStamp = mNextStamp++;
    }
    return candidate;
}

void SynthEngine::handleEvent(const NoteEvent& aEvent)
{
    // Audio thread: it is safe to touch voices directly from here.
    if (aEvent.mKind == NoteEvent::Kind::AllNotesOff)
    {
        for (auto& voice : mVoices)
        {
            voice->fastRelease();
        }
        return;
    }

    if (aEvent.mKind == NoteEvent::Kind::NoteOn)
    {
        std::uint64_t stamp = 0;
        if (Voice* voice = acquireVoice(aEvent.mVoiceType, stamp))
        {
            voice->noteOn(aEvent.mMidiNote, aEvent.mVelocity, aEvent.mSourceId, stamp);
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

    applyParamsToPool();

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
