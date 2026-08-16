#pragma once

#include "Sample.h"
#include "Voice.h"

#include <atomic>
#include <cstddef>
#include <memory>

class SampleLibrary;

/// Plays a pre-recorded one-shot, pitch-shifted by linear resampling.
///
/// This class is where two of the three relationships meet, which makes it the
/// clearest example in the codebase:
///
/// ------------------------------------------------------------------------
/// AGGREGATION -- `mSample` (shared_ptr<const Sample>)
/// ------------------------------------------------------------------------
/// The recording is *shared*, not owned exclusively. Several SamplerVoices can
/// play the same Sample at once, and the Sample outlives every one of them. The
/// shared_ptr also makes a live library reload safe: a voice that is mid-note keeps
/// its own recording alive even after the library has dropped it.
///
/// ------------------------------------------------------------------------
/// ASSOCIATION -- `mLibrary` (raw, non-owning pointer)
/// ------------------------------------------------------------------------
/// The voice *uses* a SampleLibrary to pick a recording on note-on, but it does
/// not own it and does not control its lifetime. A raw pointer states that
/// non-ownership precisely; the library is owned by the app and outlives the
/// engine. The voice simply stays silent if no library has been attached.
class SamplerVoice : public Voice
{
  public:
    VoiceType type() const override
    {
        return VoiceType::Sampler;
    }

    bool isOneShot() const override
    {
        return true;
    }

    /// Attaches the shared library. Non-owning: the caller keeps ownership.
    void setLibrary(const SampleLibrary* aLibrary)
    {
        mLibrary = aLibrary;
    }

    /// Chooses which sample a note selects. The sequencer sets this per row so one
    /// pattern can address different recordings.
    void setSampleIndex(std::size_t aIndex)
    {
        mSampleIndex.store(aIndex, std::memory_order_relaxed);
    }

    std::size_t sampleIndex() const
    {
        return mSampleIndex.load(std::memory_order_relaxed);
    }

    /// Name of the recording currently loaded, for the status panel.
    const char* sampleName() const
    {
        return mSample ? mSample->mName.c_str() : "(none)";
    }

  protected:
    void  onPrepare() override;
    void  onNoteOn() override;
    void  onParams(const VoiceParams& aParams) override;
    float generate() override;

  private:
    /// Association: used, never owned.
    const SampleLibrary* mLibrary = nullptr;

    /// Aggregation: shared with the library and with any other voice playing it.
    std::shared_ptr<const Sample> mSample;

    std::atomic<std::size_t> mSampleIndex{0};

    /// Fractional read position, in frames. double because a float loses audible
    /// precision partway through a long recording.
    double mPosition = 0.0;

    /// Frames advanced per output sample: combines pitch transposition with any
    /// mismatch between the file's rate and the device's rate.
    double mIncrement = 1.0;
};
