#include "SelfTest.h"

#include "audio/DspMath.h"
#include "audio/Envelope.h"
#include "audio/FMVoice.h"
#include "audio/Filter.h"
#include "audio/Note.h"
#include "audio/PercussionVoice.h"
#include "audio/Sample.h"
#include "audio/SamplerVoice.h"
#include "audio/WavLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace selftest
{
namespace
{

int g_checks   = 0;
int g_failures = 0;

/// Records one assertion. Returns the condition so callers can chain.
bool expect(bool aCondition, const std::string& aWhat)
{
    ++g_checks;
    if (aCondition)
    {
        std::cout << "  ok   " << aWhat << "\n";
    }
    else
    {
        std::cout << "  FAIL " << aWhat << "\n";
        ++g_failures;
    }
    return aCondition;
}

/// Renders a voice for `aSeconds` and returns the peak absolute output.
/// Used to answer the only question that really matters of a voice: does it
/// actually make a sound?
float peakOf(Voice& aVoice, float aSampleRate, float aSeconds)
{
    const int frames = static_cast<int>(aSampleRate * aSeconds);
    float     peak   = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        peak = std::max(peak, std::fabs(aVoice.render()));
    }
    return peak;
}

// -------------------------------------------------------------------------
// DSP primitives
// -------------------------------------------------------------------------

void testEnvelope()
{
    std::cout << "envelope\n";

    Envelope envelope;
    envelope.prepare(44100.0f);

    Envelope::Settings settings;
    settings.mAttack  = 0.01f;
    settings.mDecay   = 0.05f;
    settings.mSustain = 0.5f;
    settings.mRelease = 0.05f;
    envelope.setSettings(settings);

    expect(envelope.isFinished(), "starts idle");

    envelope.noteOn();
    // Run past attack and decay: the level must settle at the sustain level.
    for (int i = 0; i < 44100 / 4; ++i)
    {
        envelope.process();
    }
    expect(std::fabs(envelope.level() - settings.mSustain) < 0.02f, "settles at the sustain level");
    expect(!envelope.isFinished(), "stays active while sustaining");

    envelope.noteOff();
    bool finished = false;
    for (int i = 0; i < 44100; ++i)
    {
        envelope.process();
        if (envelope.isFinished())
        {
            finished = true;
            break;
        }
    }
    expect(finished, "release completes and the envelope frees itself");
    expect(envelope.level() == 0.0f, "ends at exactly zero");

    // REGRESSION: a percussive envelope has sustain == 0. If Decay handed over to
    // a Sustain stage at level 0 it would wait forever for a note-off that a
    // one-shot never sends, so the drum would drone and never free its voice.
    Envelope oneShot;
    oneShot.prepare(44100.0f);
    Envelope::Settings percussive;
    percussive.mAttack  = 0.001f;
    percussive.mDecay   = 0.05f;
    percussive.mSustain = 0.0f;
    percussive.mRelease = 0.05f;
    oneShot.setSettings(percussive);
    oneShot.noteOn();

    bool selfTerminated = false;
    for (int i = 0; i < 44100; ++i)
    {
        oneShot.process();
        if (oneShot.isFinished())
        {
            selfTerminated = true;
            break;
        }
    }
    expect(selfTerminated, "zero-sustain envelope terminates without a note-off");
}

void testFilter()
{
    std::cout << "filter\n";

    Filter filter;
    filter.prepare(44100.0f);
    filter.setCutoff(500.0f);
    filter.setResonance(0.5f);

    // A low-passed 5 kHz tone must come out quieter than a low-passed 100 Hz tone.
    auto runTone = [&](float aFrequency) {
        filter.reset();
        float       phase     = 0.0f;
        const float increment = aFrequency / 44100.0f;
        float       peak      = 0.0f;
        for (int i = 0; i < 8820; ++i) // 200 ms, well past the settling time
        {
            const float input  = std::sin(phase * dsp::kTwoPi);
            phase              = dsp::wrapPhase(phase + increment);
            const float output = filter.process(input);
            if (i > 4410) // ignore the transient
            {
                peak = std::max(peak, std::fabs(output));
            }
        }
        return peak;
    };

    const float lowPeak  = runTone(100.0f);
    const float highPeak = runTone(5000.0f);
    expect(lowPeak > highPeak * 2.0f, "low frequencies pass, high frequencies are attenuated");

    // Stability at maximum resonance: a self-oscillating filter that blows up is
    // the classic way to destroy someone's hearing.
    filter.reset();
    filter.setCutoff(80.0f);
    filter.setResonance(1.0f);
    bool stable = true;
    for (int i = 0; i < 44100; ++i)
    {
        const float output = filter.process((i == 0) ? 1.0f : 0.0f); // impulse
        if (!std::isfinite(output) || std::fabs(output) > 10.0f)
        {
            stable = false;
            break;
        }
    }
    expect(stable, "stays stable at maximum resonance");
}

// -------------------------------------------------------------------------
// WAV parsing
// -------------------------------------------------------------------------

/// Writes a 16-bit PCM mono WAV so the loader can be tested without any assets.
bool writeTestWav(const std::string& aPath, int aSampleRate, int aFrameCount)
{
    std::ofstream file(aPath, std::ios::binary);
    if (!file)
    {
        return false;
    }

    auto write32 = [&](std::uint32_t aValue) {
        char bytes[4] = {static_cast<char>(aValue & 0xFF), static_cast<char>((aValue >> 8) & 0xFF),
                         static_cast<char>((aValue >> 16) & 0xFF), static_cast<char>((aValue >> 24) & 0xFF)};
        file.write(bytes, 4);
    };
    auto write16 = [&](std::uint16_t aValue) {
        char bytes[2] = {static_cast<char>(aValue & 0xFF), static_cast<char>((aValue >> 8) & 0xFF)};
        file.write(bytes, 2);
    };

    const std::uint32_t dataBytes = static_cast<std::uint32_t>(aFrameCount) * 2u;

    file.write("RIFF", 4);
    write32(36u + dataBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    write32(16u);
    write16(1u); // PCM
    write16(1u); // mono
    write32(static_cast<std::uint32_t>(aSampleRate));
    write32(static_cast<std::uint32_t>(aSampleRate) * 2u); // byte rate
    write16(2u);                                           // block align
    write16(16u);                                          // bits per sample
    file.write("data", 4);
    write32(dataBytes);

    // A full-scale 441 Hz sine: exactly 10 cycles at 44100/4410.
    for (int i = 0; i < aFrameCount; ++i)
    {
        const double phase = dsp::kTwoPi * 441.0 * static_cast<double>(i) / static_cast<double>(aSampleRate);
        const auto   value = static_cast<std::int16_t>(std::sin(phase) * 32000.0);
        write16(static_cast<std::uint16_t>(value));
    }

    return file.good();
}

void testWavLoader()
{
    std::cout << "wav loader\n";

    const std::string path = "selftest_tone.wav";
    if (!writeTestWav(path, 44100, 4410))
    {
        expect(false, "could write a temporary WAV file");
        return;
    }

    Sample      sample;
    std::string error;
    const bool  loaded = wav::load(path, sample, error);

    expect(loaded, "loads a 16-bit PCM mono WAV" + (loaded ? std::string() : " (" + error + ")"));
    if (loaded)
    {
        expect(sample.frameCount() == 4410, "frame count round-trips");
        expect(std::fabs(sample.mSampleRate - 44100.0f) < 0.5f, "sample rate round-trips");

        float peak = 0.0f;
        for (float frame : sample.mFrames)
        {
            peak = std::max(peak, std::fabs(frame));
        }
        expect(peak > 0.9f && peak <= 1.0f, "samples are normalised into [-1, 1]");

        // Interpolated reads must stay in range and never return NaN.
        const float mid = sample.readInterpolated(100.5);
        expect(std::isfinite(mid) && std::fabs(mid) <= 1.0f, "interpolated read is finite and in range");
        expect(sample.readInterpolated(-1.0) == 0.0f, "negative position reads silence");
        expect(sample.readInterpolated(1.0e9) == 0.0f, "past-the-end position reads silence");
    }

    // Garbage must be rejected rather than played as noise.
    const std::string badPath = "selftest_bad.wav";
    {
        std::ofstream bad(badPath, std::ios::binary);
        bad << "this is definitely not a wav file";
    }
    Sample      badSample;
    std::string badError;
    expect(!wav::load(badPath, badSample, badError), "rejects a non-WAV file");
    expect(!wav::load("does_not_exist_at_all.wav", badSample, badError), "reports a missing file");

    std::remove(path.c_str());
    std::remove(badPath.c_str());
}

// -------------------------------------------------------------------------
// Voices (inheritance / polymorphism)
// -------------------------------------------------------------------------

void testVoices()
{
    std::cout << "voices\n";

    constexpr float kRate = 44100.0f;

    // Every voice type is driven through the *same* base-class interface, which is
    // the whole point of the Voice hierarchy.
    {
        FMVoice fm;
        fm.prepare(kRate);
        VoiceParams params;
        params.mCutoffHz = 12000.0f;
        fm.setParams(params);
        fm.noteOn(60, 1.0f, 0, 1);
        expect(peakOf(fm, kRate, 0.1f) > 0.01f, "FM voice produces sound");
        expect(fm.type() == VoiceType::FM, "FM voice reports its type");
    }

    {
        PercussionVoice drum;
        drum.prepare(kRate);
        VoiceParams params;
        params.mCutoffHz = 12000.0f;
        drum.setParams(params);
        drum.noteOn(36, 1.0f, 0, 1);
        expect(peakOf(drum, kRate, 0.05f) > 0.01f, "percussion voice produces sound");
        expect(drum.type() == VoiceType::Percussion, "percussion voice reports its type");
    }

    // REGRESSION: percussion must keep its own envelope and be a one-shot.
    {
        PercussionVoice drum;
        drum.prepare(kRate);
        expect(!drum.usesGlobalEnvelope(), "percussion keeps its own envelope");
        expect(drum.isOneShot(), "percussion is a one-shot");

        // The engine pushes the global (sustaining) ADSR every block. If that
        // overwrote the drum's zero sustain, the drum would never stop.
        VoiceParams sustaining;
        sustaining.mEnvelope.mAttack  = 0.01f;
        sustaining.mEnvelope.mDecay   = 0.10f;
        sustaining.mEnvelope.mSustain = 0.8f;
        sustaining.mEnvelope.mRelease = 0.20f;

        drum.noteOn(36, 1.0f, 0, 1);
        bool stopped = false;
        for (int i = 0; i < static_cast<int>(kRate) * 3; ++i)
        {
            if ((i % 256) == 0)
            {
                drum.setParams(sustaining);
            }
            drum.render();
            if (!drum.isActive())
            {
                stopped = true;
                break;
            }
        }
        expect(stopped, "drum still finishes while a sustaining ADSR is pushed to it");

        // A one-shot ignores note-off, so a short sequencer gate cannot chop it off.
        PercussionVoice other;
        other.prepare(kRate);
        other.noteOn(36, 1.0f, 0, 1);
        for (int i = 0; i < 64; ++i)
        {
            other.render();
        }
        other.noteOff();
        expect(!other.isReleasing(), "one-shot ignores note-off");
    }

    // The sampler stays silent (rather than crashing) with no library attached.
    {
        SamplerVoice sampler;
        sampler.prepare(kRate);
        sampler.noteOn(60, 1.0f, 0, 1);
        expect(peakOf(sampler, kRate, 0.05f) == 0.0f, "sampler is silent with no library");
    }
}

} // namespace

int run()
{
    std::cout << "cc2-synth self-test\n";
    std::cout << "-------------------\n";

    testEnvelope();
    testFilter();
    testWavLoader();
    testVoices();

    std::cout << "-------------------\n";
    std::cout << g_checks - g_failures << " / " << g_checks << " checks passed\n";

    if (g_failures > 0)
    {
        std::cout << "SELF-TEST FAILED (" << g_failures << " failure(s))\n";
        return 1;
    }

    std::cout << "SELF-TEST PASSED\n";
    return 0;
}

} // namespace selftest
