#pragma once

#include "audio/SynthEngine.h"

#include "ofMain.h"

#include <map>

/// The application root.
class ofApp : public ofBaseApp
{
  public:
    void setup() override;
    void update() override;
    void draw() override;
    void exit() override;

    void keyPressed(int aKey) override;
    void keyReleased(int aKey) override;

    /// The audio callback. This is the only openFrameworks-facing part of the audio
    /// path; it immediately delegates to the framework-independent engine.
    void audioOut(ofSoundBuffer& aBuffer) override;

  private:
    void startAudio();

    /// Maps a keyboard key to a MIDI note, or returns -1.
    int noteForKey(int aKey) const;

    SynthEngine mEngine;

    ofSoundStream mSoundStream;

    /// Which keyboard keys are currently down, so auto-repeat does not retrigger a
    /// note and so release is sent exactly once. Keyed by the raw key code.
    std::map<int, int> mHeldKeys;

    int mOctaveOffset = 0;
};
