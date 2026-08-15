#pragma once

#include "Envelope.h"
#include "Filter.h"
#include "Note.h"

#include <cstdint>

/// Parameters shared by every sounding voice, pushed down from the engine once
/// per audio block.
struct VoiceParams
{
    float mCutoffHz  = 8000.0f;
    float mResonance = 0.20f;

    /// The XY pad's Y axis, 0..1. Each voice type interprets it in the way that
    /// is musically useful for that timbre (FM index, noise tone, sample
    /// brightness) -- polymorphism applied to a control signal.
    float mMorph = 0.5f;

    Envelope::Settings mEnvelope;
};

/// Abstract base class for everything that can make a sound.
///
/// ------------------------------------------------------------------------
/// COMPOSITION (the "exclusive parts" relationship)
/// ------------------------------------------------------------------------
/// Every Voice owns an Envelope and a Filter as **by-value members**. They are
/// created with the voice, destroyed with it, and are unreachable from anywhere
/// else in the program. Neither has any meaning outside the voice it shapes,
/// which is the defining property of composition -- contrast this with the
/// Samples in SampleLibrary, which are shared (aggregation).
///
/// ------------------------------------------------------------------------
/// INHERITANCE / POLYMORPHISM
/// ------------------------------------------------------------------------
/// Subclasses supply only the raw waveform via `generate()`. The signal chain
/// itself lives here, in the non-virtual `render()`, which is the Template Method
/// pattern: it fixes the order "generate -> filter -> envelope" for all voice
/// types so no subclass can accidentally bypass its envelope (which would leave
/// the voice pool unable to tell when the note has finished) or its filter (which
/// would make the XY pad inaudible for that timbre).
///
/// The engine therefore mixes an FM voice, a drum, and a pitch-shifted recording
/// through one identical code path, and a new timbre needs no engine changes.
class Voice
{
  public:
    virtual ~Voice() = default;

    /// Called once before any audio is rendered. Subclasses may extend this via
    /// `onPrepare()`.
    void prepare(float aSampleRate);

    /// Starts a note. Non-virtual so that bookkeeping the pool relies on (pitch,
    /// source, envelope retrigger) can never be forgotten by a subclass; the
    /// per-timbre part goes in `onNoteOn()`.
    void noteOn(std::uint8_t aMidiNote, float aVelocity, std::uint8_t aSourceId, std::uint64_t aStamp);

    /// Begins the release stage. The voice keeps rendering until the release has
    /// finished -- see `isActive()`.
    ///
    /// One-shot voices (see `isOneShot()`) ignore this: a drum hit or a sampled
    /// snippet should always play to its natural length, exactly as it would on
    /// a real drum machine, instead of being cut short by a short sequencer gate.
    void noteOff();

    /// Silences the voice immediately, discarding any tail. Only used when the
    /// engine is reset; stealing goes through `noteOff()` so it stays click-free.
    void reset();

    /// Forces a very short release, whatever the voice's own envelope says.
    void fastRelease();

    /// Applies the current engine parameters. Non-virtual wrapper so the shared
    /// filter/envelope handling always happens; `onParams()` adds per-type extras.
    void setParams(const VoiceParams& aParams);

    /// Renders exactly one sample. **Template Method** -- see the class comment.
    float render();

    /// True while the voice is producing (or could still produce) sound..
    bool isActive() const
    {
        return !mEnvelope.isFinished();
    }

    bool isReleasing() const
    {
        return mEnvelope.isReleasing();
    }

    std::uint8_t midiNote() const
    {
        return mMidiNote;
    }

    std::uint8_t sourceId() const
    {
        return mSourceId;
    }

    float velocity() const
    {
        return mVelocity;
    }

    /// Monotonic note-on counter, used by the pool to steal the *oldest* voice.
    std::uint64_t stamp() const
    {
        return mStamp;
    }

    /// Current envelope gain, exposed so the visualisers can drive particle
    /// brightness/lifetime from the real synthesis state.
    float envelopeLevel() const
    {
        return mEnvelope.level();
    }

    Envelope::Stage envelopeStage() const
    {
        return mEnvelope.stage();
    }

    /// Which concrete timbre this is. Pure virtual rather than a stored field so
    /// a subclass cannot misreport itself.
    virtual VoiceType type() const = 0;

    /// Whether this voice takes its ADSR from the global (UI) settings.
    virtual bool usesGlobalEnvelope() const
    {
        return true;
    }

    /// True for voices that must always play to their natural end and therefore
    /// ignore note-off.
    virtual bool isOneShot() const
    {
        return false;
    }

  protected:
    /// Produces one raw, unfiltered, unenveloped sample in roughly [-1, 1].
    virtual float generate() = 0;

    /// Per-timbre note-start behaviour (reset phases, choose a sample, ...).
    virtual void onNoteOn() = 0;

    /// Optional hooks.
    virtual void onPrepare()
    {
    }
    virtual void onParams(const VoiceParams& aParams)
    {
        (void)aParams;
    }

    float sampleRate() const
    {
        return mSampleRate;
    }

    float frequency() const
    {
        return mFrequency;
    }

    const VoiceParams& params() const
    {
        return mParams;
    }

    // --- Composition: exclusively owned parts, by value. ---
    Envelope mEnvelope;
    Filter   mFilter;

  private:
    VoiceParams   mParams;
    float         mSampleRate = 44100.0f;
    float         mFrequency  = 261.63f;
    float         mVelocity   = 1.0f;
    std::uint8_t  mMidiNote   = 60;
    std::uint8_t  mSourceId   = 0;
    std::uint64_t mStamp      = 0;
};
