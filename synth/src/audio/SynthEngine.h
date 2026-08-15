#pragma once

#include "EventQueue.h"
#include "Note.h"
#include "Voice.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

/// Polyphonic synthesiser: owns the voices, mixes them, and is the sole writer of
/// the audio output buffer.
///
/// ------------------------------------------------------------------------
/// COMPOSITION (the whole that owns its parts)
/// ------------------------------------------------------------------------
/// `mVoices` is a `vector<unique_ptr<Voice>>`: the engine creates every voice in
/// `prepare()` and destroys them all with itself. No other object may keep a
/// voice alive, and no voice is reachable except through the engine. Combined with
/// each Voice owning its Envelope and Filter by value, the ownership tree is
/// strictly hierarchical -- destroying the engine destroys everything below it,
/// with no possibility of a dangling reference.
///
/// The `unique_ptr` indirection (rather than a vector of concrete voices) is what
/// makes the polymorphism possible: the pool holds `Voice*` and calls
/// `render()` on each without knowing or caring which subclass it is.
class SynthEngine
{
  public:
    /// Number of simultaneous notes the engine can sound.
    static constexpr std::size_t kMaxVoices = 16;

    SynthEngine();
    ~SynthEngine();

    // Non-copyable: it owns unique_ptrs and is referenced by the UI objects.
    SynthEngine(const SynthEngine&)            = delete;
    SynthEngine& operator=(const SynthEngine&) = delete;

    /// Builds the voice pool for the given device rate. Must be called before
    /// `audioOut()`. Safe to call again at a different rate (rebuilds the pool).
    ///
    /// **The audio stream must be stopped first.**
    void prepare(float aSampleRate, std::size_t aOutputChannels);

    // --- Note input (called from the UI/sequencer thread) ---------------------

    /// Enqueues a note-on. Thread-safe, non-blocking, never allocates.
    void noteOn(VoiceType aType, int aMidiNote, float aVelocity, std::uint8_t aSourceId = 0);

    /// Enqueues a note-off matching the pitch/source of a previous note-on.
    void noteOff(VoiceType aType, int aMidiNote, std::uint8_t aSourceId = 0);

    // --- Parameters (thread-safe, lock-free) ---------------------------------

    void setCutoff(float aHz);
    void setResonance(float aValue);
    void setMorph(float aValue);
    void setEnvelope(const Envelope::Settings& aSettings);

    float cutoff() const
    {
        return mCutoff.load(std::memory_order_relaxed);
    }
    float resonance() const
    {
        return mResonance.load(std::memory_order_relaxed);
    }
    float morph() const
    {
        return mMorph.load(std::memory_order_relaxed);
    }
    Envelope::Settings envelopeSettings() const;

    // --- Audio thread --------------------------------------------------------

    /// Fills `aBuffer` with `aNumFrames` interleaved frames of `aNumChannels`.
    /// Takes a raw pointer rather than an ofSoundBuffer so the whole engine stays
    /// free of openFrameworks.
    void process(float* aBuffer, std::size_t aNumFrames, std::size_t aNumChannels);

    float sampleRate() const
    {
        return mSampleRate;
    }

    /// Read-only view of the pool.
    const std::vector<std::unique_ptr<Voice>>& voices() const
    {
        return mVoices;
    }

  private:
    void   handleEvent(const NoteEvent& aEvent);
    Voice* acquireVoice(VoiceType aType);
    void   applyParamsToPool();

    // ---- Composition: the engine exclusively owns every voice. ----
    std::vector<std::unique_ptr<Voice>> mVoices;

    EventQueue<NoteEvent, 256> mEvents;

    // The class contract above promises the audio thread never locks. A
    // std::atomic is only guaranteed lock-free for a few types, so let the
    // compiler prove the assumption rather than trusting it: if a target ever
    // implements these with a mutex, every parameter read inside audioOut()
    // would silently start locking. Both targets here (Apple clang x86_64/arm64,
    // MinGW g++ x86_64) satisfy this.
    static_assert(std::atomic<float>::is_always_lock_free, "audioOut() must never lock on a float parameter");
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "audioOut() must never lock on a size_t counter");

    // Control parameters. Atomic because the UI thread writes them while the
    // audio thread reads them every block.
    std::atomic<float> mCutoff{8000.0f};
    std::atomic<float> mResonance{0.20f};
    std::atomic<float> mMorph{0.5f};
    std::atomic<float> mAttack{0.01f};
    std::atomic<float> mDecay{0.20f};
    std::atomic<float> mSustain{0.70f};
    std::atomic<float> mRelease{0.30f};

    float       mSampleRate     = 44100.0f;
    std::size_t mOutputChannels = 2;
};
