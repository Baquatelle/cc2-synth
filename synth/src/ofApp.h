#pragma once

#include "audio/SampleLibrary.h"
#include "audio/SynthEngine.h"
#include "ui/Oscilloscope.h"
#include "ui/ParticleField.h"
#include "ui/Sequencer.h"
#include "ui/XYPad.h"

#include "ofMain.h"

#include <map>
#include <string>

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
    void mousePressed(int aX, int aY, int aButton) override;
    void mouseDragged(int aX, int aY, int aButton) override;
    void mouseReleased(int aX, int aY, int aButton) override;
    void windowResized(int aW, int aH) override;

    /// The audio callback. This is the only openFrameworks-facing part of the audio
    /// path; it immediately delegates to the framework-independent engine.
    void audioOut(ofSoundBuffer& aBuffer) override;

  private:
    void startAudio();

    void loadSamples();
    void layout();

    /// Maps a keyboard key to a MIDI note, or returns -1.
    int noteForKey(int aKey) const;

    /// Screen position for a note's particle burst.
    glm::vec2 particleOriginFor(int aMidiNote) const;

    // ---- Owned by value: composition. ----
    SynthEngine   mEngine;
    SampleLibrary mSampleLibrary;
    Sequencer     mSequencer;
    XYPad         mXyPad;
    Oscilloscope  mOscilloscope;
    ParticleField mParticles;

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

    // Layout rectangles, recomputed on resize.
    ofRectangle mScopeRect;
    ofRectangle mPadRect;
    ofRectangle mGridRect;

    float mLastFrameTime = 0.0f;
};
