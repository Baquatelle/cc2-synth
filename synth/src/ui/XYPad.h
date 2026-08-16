#pragma once

class SynthEngine;

/// Draggable pad that morphs the timbre in real time.
///
/// ASSOCIATION: holds a non-owning `SynthEngine *`. Unlike the visualisers this one
/// is *not* const, because the pad's whole purpose is to write parameters -- an
/// association can be read-write; what makes it association rather than composition
/// is the absence of ownership, not the absence of mutation.
class XYPad
{
  public:
    void attach(SynthEngine* aEngine)
    {
        mEngine = aEngine;
    }

    /// Applies a pointer position if it falls inside the pad. Returns true if the
    /// event was consumed.
    bool handleDrag(float aMouseX, float aMouseY, float aX, float aY, float aWidth, float aHeight);

    void draw(float aX, float aY, float aWidth, float aHeight) const;

  private:
    /// Association: driven, never owned.
    SynthEngine* mEngine = nullptr;

    // Last normalised position, kept purely for drawing the crosshair.
    float mNormalizedX = 0.5f;
    float mNormalizedY = 0.5f;
};
