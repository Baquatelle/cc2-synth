#include "SelfTest.h"

#include "audio/DspMath.h"
#include "audio/Envelope.h"
#include "audio/Filter.h"

#include <algorithm>
#include <cmath>
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

} // namespace

int run()
{
    std::cout << "cc2-synth self-test\n";
    std::cout << "-------------------\n";

    testEnvelope();
    testFilter();

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
