#pragma once

#include <vector>

class SynthEngine;

/// Draws the synthesiser's master output as a waveform.
///
/// ASSOCIATION: holds a `const SynthEngine *` -- it observes the engine and never
/// modifies or owns it. The pointer is `const` to make that read-only relationship
/// explicit in the type system rather than merely in a comment.
class Oscilloscope
{
  public:
    void attach(const SynthEngine* aEngine)
    {
        mEngine = aEngine;
    }

    /// Pulls a fresh copy of the waveform.
    /// Called once per frame from update(), keeping the copy out of draw().
    void update();

    void draw(float aX, float aY, float aWidth, float aHeight) const;

  private:
    const SynthEngine* mEngine = nullptr;

    /// Scratch buffer, reused every frame so drawing does not allocate.
    std::vector<float> mSamples;
};
