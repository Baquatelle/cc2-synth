#include "SelfTest.h"

#include "audio/Delay.h"
#include "audio/DspMath.h"
#include "audio/Envelope.h"
#include "audio/FMVoice.h"
#include "audio/Filter.h"
#include "audio/Note.h"
#include "audio/PercussionVoice.h"
#include "audio/Sample.h"
#include "audio/SampleLibrary.h"
#include "audio/SamplerVoice.h"
#include "audio/SynthEngine.h"
#include "audio/WavLoader.h"
#include "ui/Sequencer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

void testDelay()
{
    std::cout << "delay\n";

    constexpr float kSampleRate = 44100.0f;

    // --- bypass transparency: mix==0 must pass the input through unchanged ---
    {
        Delay delay;
        delay.prepare(kSampleRate);
        delay.setMix(0.0f);
        delay.setFeedback(0.5f);
        delay.setTimeMs(100.0f);

        bool transparent = true;
        for (int i = 0; i < 1000; ++i)
        {
            const float input  = (i % 2 == 0) ? 1.0f : -0.5f;
            const float output = delay.process(input);
            if (output != input)
            {
                transparent = false;
                break;
            }
        }
        expect(transparent, "delay with mix==0 passes audio through unchanged");
    }

    // --- feedback decay ratio: each successive echo must be quieter by ~feedback ---
    {
        // Use a delay time of exactly 100 ms and a sample rate of 44100, feeding
        // one impulse.  The nth echo should be approximately feedback^n of the
        // first echo.
        constexpr float kFeedback  = 0.5f;
        constexpr float kTimeMs    = 100.0f;
        constexpr int   kDelaySamp = static_cast<int>(kSampleRate * kTimeMs / 1000.0f);

        Delay delay;
        delay.prepare(kSampleRate);
        delay.setMix(1.0f); // fully wet so we only hear the delayed signal
        delay.setFeedback(kFeedback);
        delay.setTimeMs(kTimeMs);

        // Feed an impulse at sample 0, then silence.
        std::vector<float> out;
        out.reserve(kDelaySamp * 4 + 1);
        for (int i = 0; i < kDelaySamp * 4 + 1; ++i)
        {
            out.push_back(delay.process(i == 0 ? 1.0f : 0.0f));
        }

        // Collect the peak around each expected echo position.
        auto echoAt = [&](int n) {
            const int centre = n * kDelaySamp;
            float     peak   = 0.0f;
            for (int k = centre - 2; k <= centre + 2; ++k)
            {
                if (k >= 0 && k < static_cast<int>(out.size()))
                    peak = std::max(peak, std::fabs(out[k]));
            }
            return peak;
        };

        const float echo1 = echoAt(1);
        const float echo2 = echoAt(2);
        const float echo3 = echoAt(3);

        // Each repeat should be within 10 % of the ideal feedback^n decay.
        expect(echo1 > 0.5f, "first echo is audible");
        expect(echo2 > 0.0f && std::fabs(echo2 / echo1 - kFeedback) < 0.1f,
               "second echo decays by the feedback factor");
        expect(echo3 > 0.0f && std::fabs(echo3 / echo2 - kFeedback) < 0.1f,
               "third echo decays by the feedback factor");
    }
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

// -------------------------------------------------------------------------
// Output level and percussion decay shape
// -------------------------------------------------------------------------

void testAudioLevels()
{
    std::cout << "output level and decay shape\n";

    constexpr float kRate = 44100.0f;

    // REGRESSION: a single note must arrive at a usable level.
    //
    // The engine divides the mix by sqrt(kVoicesPerType) for polyphony headroom.
    // Without a makeup gain that left one note near 0.18 (about -15 dBFS), so the
    // synth was roughly 9 dB quieter than intended and "correct" only if the user
    // turned the system volume up. A single note should land nearer -6 dBFS.
    {
        SynthEngine engine;
        engine.prepare(kRate, 2);
        // Open the filter so this measures the master gain, not the low-pass.
        engine.setCutoff(18000.0f);
        engine.noteOn(VoiceType::FM, 60, 0.95f, 0);

        std::vector<float> block(256 * 2, 0.0f);
        float              peak = 0.0f;
        for (int i = 0; i < 40; ++i)
        {
            engine.process(block.data(), 256, 2);
            for (float sample : block)
            {
                peak = std::max(peak, std::fabs(sample));
            }
        }

        expect(peak > 0.30f, "a single note reaches a healthy output level (makeup gain applied)");
    }

    // REGRESSION: the makeup gain must stay *inside* dsp::softClip().
    //
    // tanh() is the only thing bounding the master bus, so gain applied after it
    // would push the output past full scale and clip in the driver. A single note
    // cannot detect that mistake -- post-limiter it would still only reach
    // tanh(0.95 * 0.25) * 0.75 * 3.0 ~= 0.52 -- so this fills the FM sub-pool at
    // full velocity with the master volume wide open, where the same mistake
    // drives the peak towards 3.0 instead.
    //
    // Both bounds are asserted together on purpose: "stays within full scale"
    // would prove nothing if the chord were silent, and "drives the limiter hard"
    // would prove nothing if it were clipping. The lower bound is deliberately as
    // high as 0.9: the point is to prove tanh() is genuinely saturated, which is
    // the only condition under which a post-limiter gain would show up at all.
    // This configuration measures exactly 1.0000, so the margin is ample.
    {
        SynthEngine engine;
        engine.prepare(kRate, 2);
        engine.setCutoff(18000.0f);
        engine.setMasterVolume(1.0f);
        for (int note = 48; note < 48 + static_cast<int>(SynthEngine::kVoicesPerType); ++note)
        {
            engine.noteOn(VoiceType::FM, note, 1.0f, 0);
        }

        std::vector<float> block(256 * 2, 0.0f);
        float              peak = 0.0f;
        for (int i = 0; i < 40; ++i)
        {
            engine.process(block.data(), 256, 2);
            for (float sample : block)
            {
                peak = std::max(peak, std::fabs(sample));
            }
        }

        expect(peak > 0.9f && peak <= 1.0f, "a full chord drives the limiter hard and stays within full scale");
    }

    // REGRESSION: a drum must ring for something close to its nominal decay.
    //
    // Every hit is shaped by two exponentials multiplied together -- mBodyLevel
    // and the ADSR decay. With both set to the same time constant their product
    // collapsed at roughly twice the rate, so a kick with a 0.32 s decay died to
    // -40 dB in ~0.09 s and read as a click rather than a thud.
    {
        // Mirrors kKickDecay in PercussionVoice.cpp, which is file-local there.
        constexpr float kNominalKickDecay = 0.32f;

        PercussionVoice kick;
        kick.prepare(kRate);
        VoiceParams params;
        params.mCutoffHz = 18000.0f;
        kick.setParams(params);
        kick.noteOn(36, 1.0f, 0, 1); // 36 % 3 == 0 -> Kick, nominal 0.32 s decay

        const auto totalFrames       = static_cast<std::size_t>(kRate);
        const auto nominalDecayFrame = static_cast<std::size_t>(kNominalKickDecay * kRate);

        // A single pass over exactly one second, sampling the envelope as it goes
        // rather than splitting the render in two. Initialised to 0.0f so that a
        // sample point which somehow never fires *fails* the assertion below
        // instead of reading a stale value.
        float              envelopeAtNominalDecay = 0.0f;
        std::vector<float> rendered;
        rendered.reserve(totalFrames);
        for (std::size_t i = 0; i < totalFrames; ++i)
        {
            rendered.push_back(kick.render());
            if (i + 1 == nominalDecayFrame)
            {
                envelopeAtNominalDecay = kick.envelopeLevel();
            }
        }

        float peak = 0.0f;
        for (float sample : rendered)
        {
            peak = std::max(peak, std::fabs(sample));
        }

        // Last moment the hit is still within 40 dB of its own peak, i.e. how long
        // it stays genuinely audible.
        std::size_t lastAudible = 0;
        for (std::size_t i = 0; i < rendered.size(); ++i)
        {
            if (std::fabs(rendered[i]) > peak * 0.01f)
            {
                lastAudible = i;
            }
        }
        const float audibleSeconds = static_cast<float>(lastAudible) / kRate;

        // Deliberately kept, and not a duplicate of testVoices()' "percussion
        // voice produces sound": audibleSeconds is measured *relative* to the
        // hit's own peak, so it is scale-invariant, and a denormal-quiet kick with
        // a flawless decay shape would satisfy it. This anchors the absolute level
        // so that the shape measurement means something. (An outright silent kick
        // is caught either way -- peak would be 0, no sample would clear the
        // threshold, and audibleSeconds would come out 0.)
        expect(peak > 0.01f, "kick reaches a real level, so the decay measurement is meaningful");

        // The mechanism, asserted directly and independently of amplitude: at the
        // nominal decay time the envelope must still be well clear of silence, so
        // that it is the body contour -- not the envelope -- shaping the hit. With
        // both decays set to the same time this sat at ~1e-4, on the cusp of
        // dsp::kSilence and about to hand over to Idle; at 1.8x it is ~6e-3. That
        // 60x separation is what makes this the durable check, and the -40 dB
        // duration below merely the perceptual one.
        expect(envelopeAtNominalDecay > 1.0e-3f, "the envelope outlives the body contour at the nominal decay time");
        expect(audibleSeconds > 0.11f, "kick sustains its body rather than collapsing into a click");
    }

    // REGRESSION: lengthening the envelope must not stop a drum self-terminating,
    // and must not make it sit on its pool slot for long either. The envelope now
    // outlives the body contour, so it alone decides both -- that the zero sustain
    // still hands over to Idle, and how long the slot stays occupied.
    {
        PercussionVoice kick;
        kick.prepare(kRate);
        kick.noteOn(36, 1.0f, 0, 1);

        const int limitFrames = static_cast<int>(kRate) * 3;

        // `finished` carries the "terminates at all" intent directly, rather than
        // leaning on a duration threshold that would silently be a second copy of
        // limitFrames. lifetimeSeconds keeps a sentinel so a voice that never frees
        // itself *fails* the promptness bound instead of passing it vacuously.
        bool  finished        = false;
        float lifetimeSeconds = 999.0f;
        for (int i = 0; i < limitFrames; ++i)
        {
            kick.render();
            if (!kick.isActive())
            {
                finished        = true;
                lifetimeSeconds = static_cast<float>(i + 1) / kRate;
                break;
            }
        }

        expect(finished, "a drum with the lengthened envelope still frees its voice");

        // Upper bound on the pool-slot cost. A kick is ~0.58 s at 1.8x; raising
        // kEnvelopeDecayRatio to 4x would cost 1.28 s and fail here, which is the
        // point -- the percussion sub-pool is only kVoicesPerType deep, so buying
        // audibility with slot lifetime would starve a dense pattern instead.
        expect(lifetimeSeconds < 1.0f, "a drum releases its pool slot promptly");
    }
}

// -------------------------------------------------------------------------
// Aggregation: shared samples
// -------------------------------------------------------------------------

void testSampleSharing()
{
    std::cout << "sample sharing (aggregation)\n";

    auto sample           = std::make_shared<Sample>();
    sample->mName         = "test";
    sample->mSampleRate   = 44100.0f;
    sample->mBaseMidiNote = 60.0f;
    sample->mFrames.resize(4410);
    for (std::size_t i = 0; i < sample->mFrames.size(); ++i)
    {
        const double phase = dsp::kTwoPi * 441.0 * static_cast<double>(i) / 44100.0;
        sample->mFrames[i] = static_cast<float>(std::sin(phase));
    }

    SampleLibrary library;
    library.add(sample);
    expect(library.size() == 1, "library holds the sample");

    const long useCountInLibrary = sample.use_count();

    {
        // Two voices play the same recording simultaneously.
        SamplerVoice a;
        SamplerVoice b;
        a.prepare(44100.0f);
        b.prepare(44100.0f);
        a.setLibrary(&library);
        b.setLibrary(&library);
        a.noteOn(60, 1.0f, 0, 1);
        b.noteOn(72, 1.0f, 0, 2); // an octave up: exercises the pitch shift

        expect(sample.use_count() > useCountInLibrary, "several voices share one Sample (aggregation)");
        expect(peakOf(a, 44100.0f, 0.05f) > 0.01f, "sampler plays the shared recording");
        expect(peakOf(b, 44100.0f, 0.05f) > 0.01f, "a second voice plays the same recording, transposed");
    }

    // The recording outlives every voice that played it -- the defining property
    // that makes this aggregation rather than composition.
    expect(sample.use_count() == useCountInLibrary, "Sample outlives the voices that played it");

    // A voice mid-note keeps its recording alive even if the library is cleared.
    {
        SamplerVoice voice;
        voice.prepare(44100.0f);
        voice.setLibrary(&library);
        voice.noteOn(60, 1.0f, 0, 1);
        voice.render();

        library.clear();
        expect(sample.use_count() >= 2, "a sounding voice keeps its Sample alive after a library reload");

        bool finite = true;
        for (int i = 0; i < 1000; ++i)
        {
            if (!std::isfinite(voice.render()))
            {
                finite = false;
                break;
            }
        }
        expect(finite, "the voice keeps rendering safely after the library was cleared");
    }
}

// -------------------------------------------------------------------------
// The engine: pool, stealing, mixing
// -------------------------------------------------------------------------

void testEngine()
{
    std::cout << "engine\n";

    SynthEngine engine;
    engine.prepare(44100.0f, 2);

    expect(engine.voices().size() == SynthEngine::kVoicesPerType * kVoiceTypeCount, "pool is fully preallocated");
    expect(engine.activeVoiceCount() == 0, "no voices active before any note");

    std::vector<float> block(256 * 2, 0.0f);

    // Silence in, silence out.
    engine.process(block.data(), 256, 2);
    float peak = 0.0f;
    for (float sample : block)
    {
        peak = std::max(peak, std::fabs(sample));
    }
    expect(peak == 0.0f, "outputs silence when idle");

    // One note must actually be audible.
    engine.noteOn(VoiceType::FM, 60, 1.0f, 0);
    peak = 0.0f;
    for (int i = 0; i < 20; ++i)
    {
        engine.process(block.data(), 256, 2);
        for (float sample : block)
        {
            peak = std::max(peak, std::fabs(sample));
        }
    }
    expect(peak > 0.005f, "a note produces audible output");
    expect(engine.activeVoiceCount() > 0, "the note occupies a voice");

    // Both channels must carry the same signal (the synth is mono, fanned out).
    engine.process(block.data(), 256, 2);
    bool channelsMatch = true;
    for (std::size_t frame = 0; frame < 256; ++frame)
    {
        if (block[frame * 2] != block[frame * 2 + 1])
        {
            channelsMatch = false;
            break;
        }
    }
    expect(channelsMatch, "left and right carry the same samples");

    // Note-stealing: ask for far more notes than the sub-pool can hold.
    for (int note = 40; note < 40 + static_cast<int>(SynthEngine::kVoicesPerType) * 2; ++note)
    {
        engine.noteOn(VoiceType::FM, note, 0.8f, 0);
    }
    engine.process(block.data(), 256, 2);
    expect(engine.activeVoiceCount() <= SynthEngine::kVoicesPerType * kVoiceTypeCount,
           "the pool is never exceeded when notes are stolen");
    expect(engine.droppedEvents() == 0, "no events dropped");

    // REGRESSION: allNotesOff() must only enqueue. Applying it directly from the
    // calling thread would be a data race against process().
    engine.allNotesOff();
    expect(engine.activeVoiceCount() > 0, "allNotesOff() does not mutate the pool from the caller's thread");

    for (int i = 0; i < 500 && engine.activeVoiceCount() > 0; ++i)
    {
        engine.process(block.data(), 256, 2);
    }
    expect(engine.activeVoiceCount() == 0, "allNotesOff() silences everything once the queue is drained");

    // REGRESSION: the output must never contain NaN/Inf or exceed full scale, even
    // with a dense chord through a highly resonant filter.
    {
        SynthEngine stress;
        stress.prepare(44100.0f, 2);
        stress.setResonance(0.95f);
        stress.setCutoff(60.0f);
        stress.setMasterVolume(1.0f);
        for (int note = 36; note < 60; ++note)
        {
            stress.noteOn(VoiceType::FM, note, 1.0f, 0);
            stress.noteOn(VoiceType::Percussion, note, 1.0f, 0);
        }

        bool clean = true;
        for (int b = 0; b < 300 && clean; ++b)
        {
            stress.process(block.data(), 256, 2);
            for (float sample : block)
            {
                if (!std::isfinite(sample) || std::fabs(sample) > 1.001f)
                {
                    clean = false;
                    break;
                }
            }
        }
        expect(clean, "output stays finite and within full scale under stress");
    }

    // The scope buffer must be readable and sane for the oscilloscope.
    std::vector<float> scope;
    engine.copyScope(scope);
    expect(scope.size() == SynthEngine::kScopeSize, "scope buffer has the expected size");
    bool scopeFinite = true;
    for (float sample : scope)
    {
        if (!std::isfinite(sample))
        {
            scopeFinite = false;
            break;
        }
    }
    expect(scopeFinite, "scope buffer contains only finite samples");
}

// -------------------------------------------------------------------------
// Sequencer timing and association
// -------------------------------------------------------------------------

void testSequencer()
{
    std::cout << "sequencer\n";

    SynthEngine engine;
    engine.prepare(44100.0f, 2);

    Sequencer sequencer;
    sequencer.attach(&engine); // association: driven, not owned
    sequencer.clear();
    sequencer.setTempo(120.0f);

    expect(!sequencer.isPlaying(), "starts stopped");

    // Nothing should fire while stopped.
    sequencer.advance(1.0f);
    expect(sequencer.triggeredNoteCount() == 0, "no notes fire while stopped");

    // One step on row 0. At 120 BPM a sixteenth is 0.125 s, so a full 16-step bar
    // is 2 s; advancing that far must trigger the step exactly once.
    sequencer.toggleStep(0, 4);
    expect(sequencer.stepEnabled(0, 4), "toggling a step enables it");

    sequencer.setPlaying(true);
    for (int i = 0; i < 160; ++i)
    {
        sequencer.advance(0.0125f); // 2 s total, in small increments
    }
    expect(sequencer.triggeredNoteCount() >= 1, "the enabled step fires within one bar");

    // A single huge delta must not silently swallow steps.
    Sequencer burst;
    burst.attach(&engine);
    burst.clear();
    burst.setTempo(120.0f);
    for (std::size_t step = 0; step < Sequencer::kSteps; ++step)
    {
        burst.toggleStep(0, step);
    }
    burst.setPlaying(true);
    burst.advance(2.0f); // a whole bar in one frame
    expect(burst.triggeredNoteCount() >= Sequencer::kSteps - 1, "a long frame still fires every step it passed");

    // Toggling off works, and out-of-range access is safe.
    sequencer.toggleStep(0, 4);
    expect(!sequencer.stepEnabled(0, 4), "toggling again disables the step");
    expect(!sequencer.stepEnabled(9999, 9999), "out-of-range queries are safe");

    sequencer.clear();
    bool allClear = true;
    for (std::size_t row = 0; row < Sequencer::kRows; ++row)
    {
        for (std::size_t step = 0; step < Sequencer::kSteps; ++step)
        {
            if (sequencer.stepEnabled(row, step))
            {
                allClear = false;
            }
        }
    }
    expect(allClear, "clear() empties the grid");

    // Randomise is deterministic for a given seed, which keeps demos repeatable.
    Sequencer a;
    Sequencer b;
    a.setupDefaultPattern();
    b.setupDefaultPattern();
    a.randomize(12345u);
    b.randomize(12345u);
    bool identical = true;
    for (std::size_t row = 0; row < Sequencer::kRows && identical; ++row)
    {
        for (std::size_t step = 0; step < Sequencer::kSteps; ++step)
        {
            if (a.stepEnabled(row, step) != b.stepEnabled(row, step))
            {
                identical = false;
                break;
            }
        }
    }
    expect(identical, "randomize() is deterministic for a given seed");

    // Tempo is clamped to a sane musical range.
    sequencer.setTempo(1.0f);
    expect(sequencer.tempo() >= 40.0f, "tempo is clamped from below");
    sequencer.setTempo(10000.0f);
    expect(sequencer.tempo() <= 240.0f, "tempo is clamped from above");

    // A detached sequencer must not crash -- the association is optional.
    Sequencer orphan;
    orphan.setupDefaultPattern();
    orphan.setPlaying(true);
    orphan.advance(1.0f);
    expect(true, "a sequencer with no engine attached runs harmlessly");
}

} // namespace

int run()
{
    std::cout << "cc2-synth self-test\n";
    std::cout << "-------------------\n";

    testEnvelope();
    testFilter();
    testDelay();
    testWavLoader();
    testVoices();
    testAudioLevels();
    testSampleSharing();
    testEngine();
    testSequencer();

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
