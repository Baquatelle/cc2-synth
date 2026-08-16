#include "ofApp.h"

#include <algorithm>
#include <cctype>

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
    mOscilloscope.attach(&mEngine);

    startAudio();
    layout();
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

    mSoundStream.setup(settings);
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
}

void ofApp::windowResized(int aW, int aH)
{
    (void)aW;
    (void)aH;
    layout();
}

void ofApp::update()
{
    mOscilloscope.update();
}

void ofApp::draw()
{
    mOscilloscope.draw(mScopeRect.x, mScopeRect.y, mScopeRect.width, mScopeRect.height);
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
    case 'z':
    case 'Z':
        mOctaveOffset = std::max(mOctaveOffset - 1, -3);
        return;
    case 'x':
    case 'X':
        mOctaveOffset = std::min(mOctaveOffset + 1, 3);
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

void ofApp::exit()
{
    mSoundStream.close();
}
