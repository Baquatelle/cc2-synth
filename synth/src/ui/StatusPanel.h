#pragma once

#include "../audio/Note.h"

#include <string>

class SynthEngine;
class Sequencer;
class SampleLibrary;

/// Text readout of what the synthesiser is actually doing.
///
/// ASSOCIATION: holds `const` non-owning pointers to the engine, the sequencer, and
/// the sample library. It reads all three and owns none of them -- the clearest
/// demonstration that association scales to several collaborators without any
/// ownership at all.
class StatusPanel
{
  public:
    void attach(const SynthEngine* aEngine, const Sequencer* aSequencer, const SampleLibrary* aLibrary);

    /// Records the audio configuration resolved at startup, so the panel can show
    /// what the fallback chain settled on.
    void setAudioInfo(const std::string& aDescription);

    /// Currently selected voice type, shown as the "armed" instrument.
    void setActiveVoiceType(VoiceType aType)
    {
        mActiveVoiceType = aType;
    }

    void setOctaveOffset(int aOffset)
    {
        mOctaveOffset = aOffset;
    }

    void draw(float aX, float aY) const;

  private:
    // Association: all observed, none owned.
    const SynthEngine*   mEngine    = nullptr;
    const Sequencer*     mSequencer = nullptr;
    const SampleLibrary* mLibrary   = nullptr;

    std::string mAudioInfo       = "(audio not started)";
    VoiceType   mActiveVoiceType = VoiceType::FM;
    int         mOctaveOffset    = 0;
};
