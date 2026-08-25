#include "SynthEngine.h"

#include "FMVoice.h"
#include "PercussionVoice.h"
#include "SampleLibrary.h"
#include "SamplerVoice.h"

#include <algorithm>
#include <cmath>

namespace
{

constexpr std::size_t typeIndex(VoiceType aType)
{
    return static_cast<std::size_t>(aType);
}

/// Makeup gain on the voice mix, applied *before* the master soft-clipper.
constexpr float kMakeupGain = 3.0f;

} // namespace

static_assert((SynthEngine::kScopeSize & (SynthEngine::kScopeSize - 1)) == 0,
              "kScopeSize must be a power of two: the scope ring wraps with a bit mask");

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
    mVoices.reserve(kVoicesPerType * kVoiceTypeCount);

    // Allocate the whole pool up front: the audio thread must never allocate, so
    // note-on can only ever pick an already-constructed voice.
    auto buildRange = [this](VoiceType aType) {
        PoolRange range;
        range.mBegin = mVoices.size();
        for (std::size_t i = 0; i < kVoicesPerType; ++i)
        {
            switch (aType)
            {
            case VoiceType::FM:
                mVoices.emplace_back(std::make_unique<FMVoice>());
                break;
            case VoiceType::Percussion:
                mVoices.emplace_back(std::make_unique<PercussionVoice>());
                break;
            case VoiceType::Sampler:
                mVoices.emplace_back(std::make_unique<SamplerVoice>());
                break;
            }
        }
        range.mEnd                = mVoices.size();
        mRanges[typeIndex(aType)] = range;
    };

    buildRange(VoiceType::FM);
    buildRange(VoiceType::Percussion);
    buildRange(VoiceType::Sampler);

    for (auto& voice : mVoices)
    {
        voice->prepare(mSampleRate);
    }

    // Re-attach the library to the freshly built sampler voices.
    setSampleLibrary(mSampleLibrary);

    applyParamsToPool();

    // Same "allocate once in prepare(), never on the audio thread" rule that
    // governs the voice pool applies to the master-bus delay line.
    mDelay.prepare(mSampleRate);
    mDelay.setTimeMs(delayTimeMs());
    mDelay.setFeedback(delayFeedback());
    mDelay.setMix(delayMix());

    mScope.fill(0.0f);
    mScopeWrite.store(0, std::memory_order_relaxed);
    mActiveVoices.store(0, std::memory_order_relaxed);
    mPeakLevel.store(0.0f, std::memory_order_relaxed);
    mSmoothedVolume = masterVolume();
    mNextStamp      = 1;
}

void SynthEngine::setSampleLibrary(const SampleLibrary* aLibrary)
{
    // Association: stored, used, never owned. Applied before the stream starts,
    // which is why walking the pool is safe here.
    mSampleLibrary = aLibrary;

    const PoolRange& range = mRanges[typeIndex(VoiceType::Sampler)];
    for (std::size_t i = range.mBegin; i < range.mEnd && i < mVoices.size(); ++i)
    {
        if (auto* sampler = dynamic_cast<SamplerVoice*>(mVoices[i].get()))
        {
            sampler->setLibrary(aLibrary);
            sampler->setSampleIndex(samplerSampleIndex());
        }
    }
}

void SynthEngine::noteOn(VoiceType aType, int aMidiNote, float aVelocity, std::uint8_t aSourceId)
{
    if (!mEvents.push(NoteEvent::noteOn(aMidiNote, aVelocity, aType, aSourceId)))
    {
        mDroppedEvents.fetch_add(1, std::memory_order_relaxed);
    }
}

void SynthEngine::noteOff(VoiceType aType, int aMidiNote, std::uint8_t aSourceId)
{
    if (!mEvents.push(NoteEvent::noteOff(aMidiNote, aType, aSourceId)))
    {
        mDroppedEvents.fetch_add(1, std::memory_order_relaxed);
    }
}

void SynthEngine::allNotesOff()
{
    // Queued, not applied here: releasing voices directly from the UI thread
    // would mutate objects that process() is concurrently rendering.
    if (!mEvents.push(NoteEvent::allNotesOff()))
    {
        mDroppedEvents.fetch_add(1, std::memory_order_relaxed);
    }
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

void SynthEngine::setMasterVolume(float aValue)
{
    mMasterVolume.store(dsp::clampf(aValue, 0.0f, 1.0f), std::memory_order_relaxed);
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

void SynthEngine::setDelayMix(float aValue)
{
    // Clamped again here (not just inside Delay::setMix) so delayMix() -- read
    // straight back by the UI, e.g. to decide whether to draw the effect as
    // "engaged" -- always reflects the value that will actually be applied.
    mDelayMix.store(dsp::clampf(aValue, 0.0f, 1.0f), std::memory_order_relaxed);
}

void SynthEngine::setDelayFeedback(float aValue)
{
    mDelayFeedback.store(dsp::clampf(aValue, 0.0f, 0.95f), std::memory_order_relaxed);
}

void SynthEngine::setDelayTimeMs(float aValue)
{
    mDelayTimeMs.store(dsp::clampf(aValue, 1.0f, Delay::kMaxDelayMs), std::memory_order_relaxed);
}

void SynthEngine::setSamplerSampleIndex(std::size_t aIndex)
{
    mSamplerSampleIndex.store(aIndex, std::memory_order_relaxed);
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
        // Voice::setParams() only adopts the envelope when the voice reports
        // usesGlobalEnvelope(), which keeps percussion's own zero-sustain shape.
        voice->setParams(params);
    }
}

Voice* SynthEngine::acquireVoice(VoiceType aType, std::uint64_t& aOutStamp)
{
    const PoolRange& range = mRanges[typeIndex(aType)];
    if (range.mBegin >= range.mEnd || range.mEnd > mVoices.size())
    {
        return nullptr;
    }

    // Prefer a genuinely free voice.
    for (std::size_t i = range.mBegin; i < range.mEnd; ++i)
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

    for (std::size_t i = range.mBegin; i < range.mEnd; ++i)
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
        for (std::size_t i = range.mBegin; i < range.mEnd; ++i)
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
            // Tell a sampler which recording to use before it starts. Done here,
            // at note-on on the audio thread, rather than by the UI thread
            // walking the pool.
            if (aEvent.mVoiceType == VoiceType::Sampler)
            {
                static_cast<SamplerVoice*>(voice)->setSampleIndex(samplerSampleIndex());
            }
            voice->noteOn(aEvent.mMidiNote, aEvent.mVelocity, aEvent.mSourceId, stamp);
        }
        return;
    }

    // NoteOff: release every voice of that type matching pitch and source. The
    // event carries its own voiceType, so switching the selected type while a key
    // is held can never misroute the release and strand a note.
    const PoolRange& range = mRanges[typeIndex(aEvent.mVoiceType)];
    for (std::size_t i = range.mBegin; i < range.mEnd && i < mVoices.size(); ++i)
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

    // Same pattern as applyParamsToPool(): pull the latest atomic settings into
    // the (audio-thread-owned) Delay once per block, not once per sample.
    mDelay.setTimeMs(delayTimeMs());
    mDelay.setFeedback(delayFeedback());
    mDelay.setMix(delayMix());

    const float targetVolume      = masterVolume();
    // ~5 ms one-pole smoothing so dragging the volume never steps or zippers.
    const float volumeCoefficient = dsp::onePoleCoefficient(0.005f, mSampleRate);
    const float mixScale          = kMakeupGain / std::sqrt(static_cast<float>(kVoicesPerType));
    std::size_t scopeWrite        = mScopeWrite.load(std::memory_order_relaxed);
    float       peak              = 0.0f;

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

        mSmoothedVolume += (targetVolume - mSmoothedVolume) * volumeCoefficient;

        // Soft-clip so a stack of simultaneous voices compresses instead of
        // clipping harshly, then guard against NaN/Inf: one bad sample reaching
        // the driver is a full-scale click.
        //
        // mixScale carries kMakeupGain and is applied *inside* softClip(), so
        // tanh() stays the hard bound on the master bus: out cannot leave
        // (-1, 1) no matter how many voices sound at once.
        float out = dsp::sanitize(dsp::softClip(mix * mixScale) * mSmoothedVolume);

        // The delay sits after the clipper (it echoes the same signal the
        // listener hears, not the raw pre-clip mix) and is sanitized again on
        // its own way out: mDelayMix defaults to 0, so when the effect is
        // bypassed this is a no-op plus one redundant clamp, not a behaviour
        // change to any of the sound that existed before this feature.
        out = dsp::sanitize(mDelay.process(out));

        const float magnitude = std::fabs(out);
        if (magnitude > peak)
        {
            peak = magnitude;
        }

        mScope[scopeWrite] = out;
        // kScopeSize is a power of two, so the wrap is a single mask.
        scopeWrite         = (scopeWrite + 1) & (kScopeSize - 1);

        for (std::size_t channel = 0; channel < aNumChannels; ++channel)
        {
            aBuffer[frame * aNumChannels + channel] = out;
        }
    }

    // Release/acquire pairs with copyScope() so the UI sees the samples that
    // belong to the cursor it reads.
    mScopeWrite.store(scopeWrite, std::memory_order_release);
    mPeakLevel.store(peak, std::memory_order_relaxed);

    std::size_t active = 0;
    for (const auto& voice : mVoices)
    {
        if (voice->isActive())
        {
            ++active;
        }
    }
    mActiveVoices.store(active, std::memory_order_relaxed);
}

void SynthEngine::copyScope(std::vector<float>& aOutSamples) const
{
    aOutSamples.resize(kScopeSize);
    // Start at the write cursor so the copy runs oldest -> newest.
    const std::size_t start = mScopeWrite.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < kScopeSize; ++i)
    {
        aOutSamples[i] = mScope[(start + i) & (kScopeSize - 1)];
    }
}
