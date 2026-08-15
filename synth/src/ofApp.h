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

    /// A note started by the computer keyboard.
    ///
    /// The voice type is stored alongside the pitch because the release must go to
    /// the type the note *started* on. Pressing 1/2/3 while a key is still held
    /// changes `mActiveVoiceType`, and releasing with the new type would leave the
    /// original note sounding forever.
    struct HeldNote
    {
        int       mMidiNote  = 60;
        VoiceType mVoiceType = VoiceType::FM;
    };

    /// Which keyboard keys are currently down, so auto-repeat does not retrigger a
    /// note and so release is sent exactly once. Keyed by the raw key code.
    std::map<int, HeldNote> mHeldKeys;

    VoiceType mActiveVoiceType = VoiceType::FM;
    int       mOctaveOffset    = 0;
};
