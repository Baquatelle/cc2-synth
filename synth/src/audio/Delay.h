#pragma once

#include <cstddef>
#include <vector>

/// A feedback delay ("echo") applied to the master mix, after the voice pool
/// has already been summed and soft-clipped.
///
/// ------------------------------------------------------------------------
/// COMPOSITION (the same relationship Voice.h documents, one level up)
/// ------------------------------------------------------------------------
/// `SynthEngine` owns exactly one `Delay` by value. The delay line has no
/// meaning or lifetime outside the engine's master bus -- it cannot be
/// shared with another engine, and it is destroyed the moment the engine is,
/// which is precisely why this is composition and not aggregation (contrast
/// with `SampleLibrary`'s `shared_ptr<const Sample>`, which several
/// `SamplerVoice`s *do* share).
///
/// Deliberately mono and deliberately simple: it is a single delay line with
/// feedback and a dry/wet mix, sized once in `prepare()` (never resized on
/// the audio thread) so it upholds the same "no allocation inside
/// `process()`" rule as the rest of the engine.
class Delay
{
  public:
    /// Longest delay time the line can be set to, in milliseconds. Bounds the
    /// one allocation this class ever makes, in `prepare()`.
    static constexpr float kMaxDelayMs = 1000.0f;

    /// Allocates the delay line for `aSampleRate`. Must be called before
    /// `process()`, exactly like `Voice::prepare()`.
    void prepare(float aSampleRate);

    /// Delay time in milliseconds, clamped to (0, kMaxDelayMs].
    void setTimeMs(float aTimeMs);

    /// How much of the delayed signal is fed back into the line, clamped to
    /// [0, 0.95]. Kept below 1 so a held note cannot build up without bound.
    void setFeedback(float aFeedback);

    /// Dry/wet balance, clamped to [0, 1]. 0 is fully bypassed (the default),
    /// so the effect is silent and inaudible until a caller opts in.
    void setMix(float aMix);

    /// Clears the line to silence, e.g. on panic.
    void reset();

    /// Processes exactly one sample. Safe to call every sample from the audio
    /// thread: no allocation, no locking, no logging.
    float process(float aInput);

  private:
    std::vector<float> mBuffer;
    std::size_t         mWriteIndex = 0;
    float               mSampleRate = 44100.0f;
    float               mTimeMs     = 220.0f;
    float               mFeedback   = 0.35f;
    float               mMix        = 0.0f;
};
