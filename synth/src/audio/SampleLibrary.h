#pragma once

#include "Sample.h"

#include <memory>
#include <string>
#include <vector>

/// Owns every loaded one-shot recording.
///
/// ------------------------------------------------------------------------
/// AGGREGATION (the "shared parts" relationship)
/// ------------------------------------------------------------------------
/// The library is the *owner* of its Samples, but it is not their exclusive
/// owner: it hands out `shared_ptr<const Sample>` to SamplerVoices, which keep
/// the recording alive for as long as they are playing it.
///
/// This is aggregation rather than composition because the parts are
/// independent of any one whole:
///   * many voices can hold the same Sample simultaneously,
///   * a Sample survives the destruction of every voice that played it,
///   * and reloading the library does not invalidate a voice that is mid-note.
class SampleLibrary
{
  public:
    /// Scans `aDirectory` for `*.wav` and loads everything it can parse.
    /// Returns the number of samples successfully loaded.
    std::size_t loadDirectory(const std::string& aDirectory);

    /// Adds an already-built sample (used by the self-test, which has no files).
    void add(std::shared_ptr<const Sample> aSample);

    void clear();

    std::size_t size() const
    {
        return mSamples.size();
    }

    bool empty() const
    {
        return mSamples.empty();
    }

    /// Shares the sample at `aIndex`, wrapping out of range. Returns nullptr only
    /// when the library is empty, which callers must handle by staying silent.
    std::shared_ptr<const Sample> at(std::size_t aIndex) const;

    const std::vector<std::shared_ptr<const Sample>>& samples() const
    {
        return mSamples;
    }

    /// Names in load order, for the status panel.
    std::vector<std::string> names() const;

  private:
    std::vector<std::shared_ptr<const Sample>> mSamples;
};
