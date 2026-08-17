#include "ofApp.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{

/// Buffer size per platform.
#ifdef TARGET_WIN32
constexpr int kBufferSize = 512;
#else
constexpr int kBufferSize = 256;
#endif

constexpr int kSampleRate = 44100;

/// Keyboard -> semitone offset from the octave root, laid out like a piano:
/// the "asdfghj" home row is the white keys, "wetyu" above it the black keys.
struct KeyMapping
{
    int mKey;
    int mSemitone;
};

constexpr KeyMapping kKeyMap[] = {
    {'a', 0}, {'w', 1}, {'s', 2},  {'e', 3},  {'d', 4},  {'f', 5},  {'t', 6},  {'g', 7},
    {'y', 8}, {'h', 9}, {'u', 10}, {'j', 11}, {'k', 12}, {'o', 13}, {'l', 14},
};

constexpr int kBaseNote = 60; // C4

} // namespace

void ofApp::setup()
{
    ofSetWindowTitle("cc2-synth");
    ofSetFrameRate(60);
    ofBackground(18);
    ofSetCircleResolution(32);

    loadSamples();

    // ---- Wire the associations. -----------------------------------------
    // Every member already exists (they are by-value members of this object), so
    // these pointers are valid for the whole run and none of them imply ownership.
    mSequencer.attach(&mEngine);
    mXyPad.attach(&mEngine);
    mOscilloscope.attach(&mEngine);
    mParticles.attach(&mEngine);
    mStatusPanel.attach(&mEngine, &mSequencer, &mSampleLibrary);

    mSequencer.setupDefaultPattern();

    startAudio();
    layout();

    mLastFrameTime = ofGetElapsedTimef();
}

void ofApp::loadSamples()
{
    // Prepared offline by scripts/bootstrap.py. ofToDataPath keeps this working
    // identically on macOS (inside the .app bundle) and on Windows (next to the
    // .exe), and forward slashes are accepted on both.
    const std::string directory = ofToDataPath("samples", true);
    const std::size_t loaded    = mSampleLibrary.loadDirectory(directory);

    if (loaded == 0)
    {
        ofLogWarning("ofApp") << "no samples found in " << directory
                              << " -- run 'python3 scripts/bootstrap.py' to generate them. "
                                 "The FM and percussion voices still work.";
    }
    else
    {
        ofLogNotice("ofApp") << "loaded " << loaded << " samples from " << directory;
    }

    // Attached before the stream starts, so no synchronisation is required.
    mEngine.setSampleLibrary(&mSampleLibrary);
}

void ofApp::startAudio()
{
    ofSoundStreamSettings settings;
    settings.setOutListener(this);
    settings.sampleRate        = kSampleRate;
    settings.numOutputChannels = 2;
    settings.numInputChannels  = 0;
    settings.bufferSize        = kBufferSize;

    // Prepare the engine *before* opening the stream, so the very first callback
    // already has a fully built voice pool to render from.
    mEngine.prepare(static_cast<float>(kSampleRate), 2);

    if (!mSoundStream.setup(settings))
    {
        // Fallback: let openFrameworks pick the default device and rate itself.
        ofLogWarning("ofApp") << "preferred audio settings rejected, retrying with device defaults";

        ofSoundStreamSettings fallback;
        fallback.setOutListener(this);
        fallback.numOutputChannels = 2;
        fallback.numInputChannels  = 0;
        fallback.bufferSize        = kBufferSize;

        if (!mSoundStream.setup(fallback))
        {
            ofLogError("ofApp") << "could not open any audio output device -- running silently";
            mStatusPanel.setAudioInfo("FAILED to open an audio device");
            return;
        }
    }

    const float actualRate = static_cast<float>(mSoundStream.getSampleRate());

    // If the device forced a different rate, rebuild at that rate.
    if (std::fabs(actualRate - static_cast<float>(kSampleRate)) > 1.0f)
    {
        ofLogNotice("ofApp") << "device chose " << actualRate << " Hz; rebuilding engine to match";
        restartAudioAtRate(actualRate);
        return;
    }

    std::string info = ofToString(mSoundStream.getSampleRate()) + " Hz, buffer " +
                       ofToString(mSoundStream.getBufferSize()) + " frames, " +
                       ofToString(mSoundStream.getNumOutputChannels()) + " ch";
#ifdef TARGET_WIN32
    info += " (Windows: larger buffer for WASAPI/DirectSound)";
#else
    info += " (macOS CoreAudio)";
#endif

    // Latency is what the buffer size actually means to the player.
    const float latencyMs =
        1000.0f * static_cast<float>(mSoundStream.getBufferSize()) / static_cast<float>(mSoundStream.getSampleRate());
    info += " ~" + ofToString(latencyMs, 1) + " ms";

    mStatusPanel.setAudioInfo(info);
    ofLogNotice("ofApp") << "audio: " << info;
}

void ofApp::restartAudioAtRate(float aSampleRate)
{
    mSoundStream.close();
    mEngine.prepare(aSampleRate, 2);

    ofSoundStreamSettings settings;
    settings.setOutListener(this);
    settings.sampleRate        = static_cast<size_t>(aSampleRate);
    settings.numOutputChannels = 2;
    settings.numInputChannels  = 0;
    settings.bufferSize        = kBufferSize;

    if (!mSoundStream.setup(settings))
    {
        ofLogError("ofApp") << "could not reopen the audio device at " << aSampleRate << " Hz";
        mStatusPanel.setAudioInfo("FAILED to reopen audio at " + ofToString(aSampleRate) + " Hz");
        return;
    }

    std::string info = ofToString(mSoundStream.getSampleRate()) + " Hz (device default), buffer " +
                       ofToString(mSoundStream.getBufferSize()) + " frames, " +
                       ofToString(mSoundStream.getNumOutputChannels()) + " ch";
    const float latencyMs =
        1000.0f * static_cast<float>(mSoundStream.getBufferSize()) / static_cast<float>(mSoundStream.getSampleRate());
    info += " ~" + ofToString(latencyMs, 1) + " ms";

    mStatusPanel.setAudioInfo(info);
    ofLogNotice("ofApp") << "audio: " << info;
}

void ofApp::audioOut(ofSoundBuffer& aBuffer)
{
    // The only openFrameworks code on the audio path: hand the raw interleaved
    // pointer straight to the framework-independent engine.
    mEngine.process(aBuffer.getBuffer().data(), aBuffer.getNumFrames(), aBuffer.getNumChannels());
}

void ofApp::layout()
{
    const float width       = static_cast<float>(ofGetWidth());
    const float height      = static_cast<float>(ofGetHeight());
    const float margin      = 16.0f;
    const float columnWidth = width - margin * 2.0f;

    mScopeRect.set(margin, margin, columnWidth, height * 0.26f);

    const float middleY  = mScopeRect.getBottom() + margin;
    const float padWidth = columnWidth * 0.36f;
    mPadRect.set(margin, middleY, padWidth, height * 0.30f);

    const float statusX = mPadRect.getRight() + margin;
    mStatusRect.set(statusX, middleY, columnWidth - padWidth - margin, height * 0.30f);

    const float gridY = mPadRect.getBottom() + margin;
    mGridRect.set(margin, gridY, columnWidth, height - gridY - margin);
}

void ofApp::windowResized(int aW, int aH)
{
    (void)aW;
    (void)aH;
    layout();
}

void ofApp::update()
{
    // Real elapsed time rather than a fixed step, so the sequencer's tempo stays
    // correct even if the frame rate wobbles.
    const float now   = ofGetElapsedTimef();
    float       delta = now - mLastFrameTime;
    mLastFrameTime    = now;

    // Clamp: after a window drag or a breakpoint, a huge delta would fire a burst
    // of steps all at once.
    delta = dsp::clampf(delta, 0.0f, 0.1f);

    mSequencer.advance(delta);
    mOscilloscope.update();
    mParticles.update(delta);

    mStatusPanel.setActiveVoiceType(mActiveVoiceType);
    mStatusPanel.setOctaveOffset(mOctaveOffset);
}

void ofApp::draw()
{
    // Particles are drawn first so the panels stay legible on top of the bloom.
    mParticles.draw();

    mOscilloscope.draw(mScopeRect.x, mScopeRect.y, mScopeRect.width, mScopeRect.height);
    mXyPad.draw(mPadRect.x, mPadRect.y, mPadRect.width, mPadRect.height);
    mSequencer.draw(mGridRect.x, mGridRect.y, mGridRect.width, mGridRect.height);
    mStatusPanel.draw(mStatusRect.x, mStatusRect.y);
}

int ofApp::noteForKey(int aKey) const
{
    // Normalise so shifted/capitalised input still plays.
    const int lower = std::tolower(aKey);
    for (const KeyMapping& mapping : kKeyMap)
    {
        if (mapping.mKey == lower)
        {
            return kBaseNote + mapping.mSemitone + mOctaveOffset * 12;
        }
    }
    return -1;
}

glm::vec2 ofApp::particleOriginFor(int aMidiNote) const
{
    // Map pitch across the width of the grid, so bursts appear where the note sits.
    const float lowest     = static_cast<float>(kBaseNote - 24);
    const float highest    = static_cast<float>(kBaseNote + 24);
    const float normalized = dsp::clampf((static_cast<float>(aMidiNote) - lowest) / (highest - lowest), 0.0f, 1.0f);

    return {mGridRect.x + normalized * mGridRect.width, mGridRect.getCenter().y};
}

void ofApp::keyPressed(int aKey)
{
    switch (aKey)
    {
    case '1':
        mActiveVoiceType = VoiceType::FM;
        return;
    case '2':
        mActiveVoiceType = VoiceType::Percussion;
        return;
    case '3':
        mActiveVoiceType = VoiceType::Sampler;
        return;
    case ' ':
        mSequencer.togglePlaying();
        return;
    case 'z':
    case 'Z':
        mOctaveOffset = std::max(mOctaveOffset - 1, -3);
        return;
    case 'x':
    case 'X':
        mOctaveOffset = std::min(mOctaveOffset + 1, 3);
        return;
    case 'c':
    case 'C':
        mSequencer.clear();
        return;
    case 'r':
    case 'R':
        mSequencer.randomize(static_cast<unsigned int>(ofGetElapsedTimeMillis()));
        return;
    case '[':
        mSequencer.setTempo(mSequencer.tempo() - 5.0f);
        return;
    case ']':
        mSequencer.setTempo(mSequencer.tempo() + 5.0f);
        return;
    case ',':
        // Cycle the sampler's recording downwards.
        if (mSampleLibrary.size() > 0)
        {
            const std::size_t count = mSampleLibrary.size();
            mEngine.setSamplerSampleIndex((mEngine.samplerSampleIndex() + count - 1) % count);
        }
        return;
    case '.':
        if (mSampleLibrary.size() > 0)
        {
            mEngine.setSamplerSampleIndex((mEngine.samplerSampleIndex() + 1) % mSampleLibrary.size());
        }
        return;
    case OF_KEY_ESC:
        mEngine.allNotesOff();
        return;
    default:
        break;
    }

    const int note = noteForKey(aKey);
    if (note < 0)
    {
        return;
    }

    // Ignore auto-repeat: holding a key must not retrigger the note every frame.
    if (mHeldKeys.find(aKey) != mHeldKeys.end())
    {
        return;
    }

    // Record the voice type along with the pitch, so the release later goes to the
    // type this note actually started on even if the selection changes meanwhile.
    mHeldKeys[aKey] = HeldNote{note, mActiveVoiceType};

    mEngine.noteOn(mActiveVoiceType, note, 0.95f, 0);

    const glm::vec2 origin = particleOriginFor(note);
    mParticles.spawn(mActiveVoiceType, note, 0.95f, origin.x, origin.y);
}

void ofApp::keyReleased(int aKey)
{
    // Look up what we actually started, rather than recomputing it: the octave or
    // the selected voice type may have changed while the key was held, and
    // releasing the wrong note or the wrong type would leave the original stuck on.
    const auto held = mHeldKeys.find(aKey);
    if (held == mHeldKeys.end())
    {
        return;
    }

    mEngine.noteOff(held->second.mVoiceType, held->second.mMidiNote, 0);
    mHeldKeys.erase(held);
}

void ofApp::mousePressed(int aX, int aY, int aButton)
{
    (void)aButton;

    const auto mouseX = static_cast<float>(aX);
    const auto mouseY = static_cast<float>(aY);

    if (mSequencer.handleClick(mouseX, mouseY, mGridRect.x, mGridRect.y, mGridRect.width, mGridRect.height))
    {
        return;
    }

    mXyPad.handleDrag(mouseX, mouseY, mPadRect.x, mPadRect.y, mPadRect.width, mPadRect.height);
}

void ofApp::mouseDragged(int aX, int aY, int aButton)
{
    (void)aButton;

    const auto mouseX = static_cast<float>(aX);
    const auto mouseY = static_cast<float>(aY);

    // Painting across the grid takes precedence over the pad, so a gesture that
    // starts on the grid keeps drawing steps.
    if (mSequencer.handleDrag(mouseX, mouseY, mGridRect.x, mGridRect.y, mGridRect.width, mGridRect.height))
    {
        return;
    }

    mXyPad.handleDrag(mouseX, mouseY, mPadRect.x, mPadRect.y, mPadRect.width, mPadRect.height);
}

void ofApp::mouseReleased(int aX, int aY, int aButton)
{
    (void)aX;
    (void)aY;
    (void)aButton;
    mSequencer.endDrag();
}

void ofApp::exit()
{
    mSoundStream.close();
}
