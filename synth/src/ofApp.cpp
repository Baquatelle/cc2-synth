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

    startAudio();
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

void ofApp::update()
{
}

void ofApp::draw()
{
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
    case 'z':
    case 'Z':
        mOctaveOffset = std::max(mOctaveOffset - 1, -3);
        return;
    case 'x':
    case 'X':
        mOctaveOffset = std::min(mOctaveOffset + 1, 3);
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

    mHeldKeys[aKey] = note;

    mEngine.noteOn(VoiceType::FM, note, 0.95f, 0);
}

void ofApp::keyReleased(int aKey)
{
    const auto held = mHeldKeys.find(aKey);
    if (held == mHeldKeys.end())
    {
        return;
    }

    mEngine.noteOff(VoiceType::FM, held->second, 0);
    mHeldKeys.erase(held);
}

void ofApp::exit()
{
    mSoundStream.close();
}
