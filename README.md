# cc2-synth — a polyphonic software synthesizer in openFrameworks

A software synthesizer built on openFrameworks 0.12.1 (C++17), running on macOS
and Windows. Every sample you hear is computed by this project inside a single
`audioOut()` audio callback — openFrameworks provides the window, the input
events, the drawing, and the audio device, and nothing above that.

- **Three sound sources:** FM, percussion, and a sampler that plays pre-recorded
  WAV one-shots
- **Playable** from the computer keyboard, a clickable step sequencer, and a
  mouse XY morph pad
- **Visualized** live with an oscilloscope, note-triggered particle blooms, and
  an engine status readout

> **Current status:** builds and runs on macOS; `--check` passes 65/65 headless
> checks. Windows support is written but not yet verified, and a handful of known
> defects remain. See **[KNOWN_ISSUES.md](KNOWN_ISSUES.md)** for the full list.

---

## 1. Quickstart

Run from the repository root. Both scripts are idempotent — re-running them is
safe and skips whatever is already done.

### macOS

```bash
python3 scripts/bootstrap.py     # fetch openFrameworks, prepare samples, generate the Xcode project
python3 scripts/build.py         # build
python3 scripts/build.py --run   # build, then launch
```

### Windows (from an **MSYS2 MinGW64** shell)

```bash
python3 scripts/bootstrap.py
python3 scripts/build.py
python3 scripts/build.py --run
```

### Headless self-test (no window, no audio device)

```bash
python3 scripts/build.py --check
```

`bootstrap.py` downloads openFrameworks 0.12.1 into `of/` (~210 MB on macOS,
~363 MB on Windows), caches the archive in `.cache/`, prepares the sample WAVs,
and runs openFrameworks' `projectGenerator` for your platform. Neither `of/` nor
the generated project files are committed.

**Prerequisites:** `python3` and `git` on both platforms; Xcode with command line
tools on macOS; an MSYS2 MinGW64 shell with `make` and a `mingw-w64-x86_64-gcc`
toolchain on Windows.

### Build options

| Command | Effect |
| --- | --- |
| `python3 scripts/build.py` | Release build |
| `python3 scripts/build.py --debug` | Debug build |
| `python3 scripts/build.py --run` | build, then launch the app |
| `python3 scripts/build.py --check` | build, then run the headless self-test (nonzero exit on failure) |

---

## 2. Controls

The **status panel in the running app is authoritative** — it shows the live key
bindings and the current engine state.

| Input | Action |
| --- | --- |
| `a s d f g h j` | play natural notes |
| `w e t y u` | play accidentals (the black keys above) |
| `z` / `x` | shift octave down / up |
| `1` `2` `3` | select the active voice type (FM / percussion / sampler) |
| **Mouse drag on the XY pad** | X = filter cutoff, Y = resonance and FM modulation index |
| **Click a sequencer cell** | toggle that step on/off (drag to paint, clicking auditions the row) |
| **Sequencer transport** | start/stop, adjust tempo, clear, randomize |
| `ESC` | panic — release all sounding notes |

Notes are polyphonic: up to 16 voices sound at once, and when the pool is full
the oldest voice is stolen.

---

## 3. Project description — architecture and class relationships

> The assignment asks which class relationships were used and why. This section
> is that answer.

### 3.1 The shape of the program

The program is two cooperating halves separated by a thread boundary:

```
                UI thread (60 fps)                    audio thread (~172 Hz)
   ┌──────────────────────────────────────┐        ┌───────────────────────────┐
   │ ofApp                                │        │ SynthEngine::audioOut()   │
   │  ├─ Sequencer   ─┐                   │        │   drain event queue       │
   │  ├─ XYPad       ─┼─ note + param ───►│ queue  │   for each Voice:         │
   │  ├─ Oscilloscope │   events          │ ═════► │     render() → Envelope   │
   │  ├─ ParticleField│                   │ atomics│                → Filter   │
   │  └─ StatusPanel ─┘                   │        │   mix, soft-clip          │
   └──────────────────────────────────────┘        └───────────────────────────┘
```

Nothing on the left ever touches a `Voice` directly. Note-on/note-off events
cross into the audio thread through a **lock-free ring-buffer queue**
(`EventQueue`), and continuous parameters (cutoff, resonance, tempo, volume)
cross as `std::atomic` values. The audio callback performs no allocation, no
locking, and no logging — those are the three classic ways to produce an audible
dropout, and the architecture is shaped primarily by avoiding them.

### 3.2 Composition — `SynthEngine` owns its voices; each `Voice` owns its DSP parts

`SynthEngine` holds its voice pool as `std::vector<std::unique_ptr<Voice>>`, and
each `Voice` holds an `Envelope` and a `Filter` as **by-value members**.

*Why composition:* this is a genuine whole-part relationship with coincident
lifetimes. An `Envelope` has no independent meaning — it is not "an envelope that
happens to be attached to a voice", it *is* part of that voice's definition, and
it is meaningless to speak of it outliving the voice or being shared with another.
The same holds for the pool: the voices exist to serve the engine's mixing loop
and are destroyed with it.

This choice also buys real-time safety, which is why it is not merely academic.
By-value members mean an `Envelope` and `Filter` are contiguous with their voice
in memory, so rendering a voice touches one cache region rather than chasing
pointers. And because the pool is allocated once in `prepare()` and thereafter
only *reused*, a note-on never allocates — it just finds an idle voice and resets
it. Composition is what makes "no allocation in the audio callback" structural
rather than a rule someone has to remember.

### 3.3 Aggregation — `SamplerVoice` shares samples from a `SampleLibrary`

`SampleLibrary` is owned by the application and hands out
`std::shared_ptr<const Sample>`. A `SamplerVoice` holds one of those pointers
while it plays.

*Why aggregation rather than composition:* the lifetimes genuinely differ, and the
sharing is the point. A single `Sample` — a decoded block of PCM audio, possibly
megabytes — is played by many voices at once, and must survive each of them. If
sixteen voices each *composed* a private copy of a snare hit, the program would
hold sixteen copies of identical audio and would have to copy megabytes during a
note-on, inside the audio callback, which is exactly what must not happen.
Aggregation lets a note-on copy a shared pointer instead: the audio is loaded once
and referenced cheaply.

This is the textbook distinction made concrete. A voice *has an* envelope
(composition: exclusive, lifetime-bound). A voice *uses a* sample (aggregation:
shared, independently owned). `shared_ptr` expresses that shared ownership
directly, and `const` documents that a playing voice may read the audio but never
modify it — which is also what makes sharing across voices safe.

### 3.4 Association — visualizers and the sequencer reference the engine

`Sequencer`, `Oscilloscope`, `ParticleField`, `StatusPanel`, and `XYPad` each hold
a **non-owning reference** to `SynthEngine`. They call into it (the sequencer
triggers notes; the XY pad sets parameters) and read from it (the panel reports
state; the scope reads the output ring buffer) without any claim on its lifetime.

*Why association:* these objects collaborate with the engine but do not contain
it and are not part of it. The engine is fully functional with no sequencer and no
visualizers attached — which is not a hypothetical, it is precisely how the
headless `--selftest` mode runs, driving the engine with no UI object in
existence. That the engine has no compile-time knowledge of its observers is what
makes that test mode possible, and it means a new visualizer can be added without
touching the audio code at all.

Direction matters here: the dependency points **one way**, from UI to engine. The
engine never calls back into the UI. If it did, the audio thread would end up
invoking drawing code, and the thread separation described above would collapse.
So the visualizers *poll* the engine each frame rather than being notified by it.

### 3.5 Inheritance and polymorphism — the `Voice` hierarchy

`Voice` is an abstract base class with a small pure-virtual interface;
`FMVoice`, `PercussionVoice`, and `SamplerVoice` implement it. `SynthEngine`
renders `Voice&` and never asks which kind it has.

Strictly this is a fourth relationship rather than one of the three named in the
assignment, but it is what keeps the other three clean: the engine's mixing loop,
the voice pool, and the note-stealing logic are written exactly once and work for
all three timbres. Adding a fourth sound source means adding one subclass and
touching no existing audio code.

The hierarchy also captures a real behavioural difference rather than just sharing
code. Percussion and sampler voices are **one-shots**: they ignore note-off and
play to their natural length, because a drum hit does not sustain while you hold
the key. The FM voice is **sustaining**: it holds until release. That distinction
lives in the subclasses, so the engine does not need to special-case drums.

### 3.6 Why these three relationships together

They are not three arbitrary illustrations of a syllabus — each is the answer to a
different question about lifetime:

| Question | Relationship | Mechanism |
| --- | --- | --- |
| Whose life is bound to mine? | composition | by-value member / `unique_ptr` |
| What do I use that outlives me and is shared? | aggregation | `shared_ptr<const T>` |
| Who do I talk to without owning? | association | non-owning reference |

Chosen this way, the ownership graph is acyclic and every object's lifetime is
obvious from its declaration — which in a program with a real-time audio thread is
a correctness property, not a matter of taste.

---

## 4. How the sound is made

- **FM voice** — a carrier oscillator whose phase is modulated by a second
  oscillator. The modulation index (mouse Y) controls how much the modulator
  deviates the carrier, moving the timbre from near-sine to bright and metallic.
- **Percussion voice** — filtered noise plus a tuned body, with a fast pitch and
  amplitude envelope. The note number selects which drum is played.
- **Sampler voice** — plays a pre-recorded 16-bit PCM WAV one-shot, pitch-shifted
  by linear resampling: playing a higher note reads through the sample faster.

Every voice then passes through its own ADSR `Envelope` and resonant low-pass
`Filter`, and the mix is soft-clipped so stacking voices cannot produce a hard
digital clip.

### Two deliberate implementation decisions

**`ofSoundPlayer` is not used anywhere.** It can only play back files, and the FM
and percussion voices do not exist as files — they are computed per sample, so a
custom `audioOut()` was required regardless. Committing fully to that path also
means sound generation is identical on both platforms, instead of resting on
openFrameworks' three different per-platform playback backends (AVFoundation on
macOS, OpenAL on MinGW, MediaFoundation on Windows).

**WAV loading is our own ~60-line RIFF parser** (`WavLoader`). openFrameworks
0.12.1 has no `ofSoundFile` class and bundles no audio decoding libraries, so
there is no core route from a file on disk to a buffer of samples. Parsing the
handful of RIFF header fields we need is small, fully portable, and keeps sample
loading byte-identical across platforms. The trade-off is that only 16-bit PCM WAV
is supported — MP3 is deliberately out of scope.

There is also **no FFT or spectrum analyzer**, by design. The chosen
visualizations do not need one, and openFrameworks' global `ofSoundGetSpectrum()`
would not have worked here anyway: it dates from the FMOD backend and cannot see
audio produced by our own `audioOut()`.

### Sample assets

`bootstrap.py` prepares `synth/bin/data/samples/` offline, with no network access
beyond the openFrameworks download, using only Python's standard-library `wave`
module:

- **kick, snare, hat, pluck** — synthesized deterministically from a fixed seed,
  so every machine gets byte-identical files
- **beat_slice, synth_slice** — cut from the `beat.wav` and `synth.wav` that ship
  with openFrameworks' own examples, sliced at the loudest transient

This satisfies the "pre-recorded sound snippets" goal with no licensing ambiguity
and nothing fragile to download. To use your own material, drop 16-bit PCM WAVs
into that folder.

---

## 5. Cross-platform notes

One source tree; the native project for each platform is generated by
openFrameworks' `projectGenerator` (Xcode on macOS, MinGW Makefiles on Windows).
Visual Studio is not a supported target.

**Audio latency differs between platforms, by design.** The engine requests a
256-frame buffer on macOS and 512 on Windows, because Windows' WASAPI and
DirectSound backends need more headroom to avoid glitching. Key-to-sound latency
is therefore noticeably higher on Windows (roughly 12 ms versus 6 ms). This is a
property of the platform audio stacks, not a bug. The resolved sample rate,
buffer size, channel count, API name and one-way latency are logged to the
console at startup, so you can see what you actually got:

```
[notice ] ofApp: audio: 44100 Hz, buffer 256 frames, 2 ch (macOS CoreAudio) ~5.8 ms
```

The engine also **negotiates rather than assumes**: it requests its preferred
device, falls back to the system default if that fails, and adopts whatever
sample rate the device actually granted instead of insisting on 44100 Hz.

Portability is enforced in the code as well: openFrameworks' `TWO_PI` rather than
`M_PI` (which MinGW does not define under strict ANSI), `std::` math functions,
`ofToDataPath` for every asset path, lowercase asset filenames so
case-sensitivity differences cannot bite, and denormal flushing in the filter and
envelope so long decay tails cannot cause CPU spikes on either platform.

MinGW builds open a console window alongside the app. That is expected, and
convenient — the audio device log prints there.

---

## 6. Verification

`--selftest` runs the DSP and engine logic headlessly, with no window and no
audio device, so it works over SSH and in CI. It branches in `main.cpp` before
any window or sound stream is created, and exits nonzero on failure. It covers
envelope stage progression and zero-sustain termination, filter stability,
RIFF parsing, voice-pool allocation and note stealing, one-shot versus sustaining
release behaviour, master output gain staging, percussion decay shape, and
sequencer timing.

GitHub Actions runs the full bootstrap → build → `--selftest` sequence on both
`macos-latest` and `windows-latest` for every push, so a change that breaks only
the platform you are not sitting in front of gets caught.

Because a self-test cannot tell you whether something *sounds* right, each
platform additionally gets a manual pass: every voice type sounds from the
keyboard, the sequencer loops and triggers, the XY pad audibly morphs the timbre,
and the oscilloscope tracks the output without dropouts.

---

## 7. Repository layout

```
PLAN.md                      the agreed project plan
scripts/bootstrap.py         fetch openFrameworks, prepare samples, generate the project
scripts/build.py             build / run / --check
.github/workflows/ci.yml     macOS + Windows CI
synth/src/
  main.cpp                   entry point; --selftest branches before any window
  ofApp.{h,cpp}              owns the engine and all UI objects
  SelfTest.{h,cpp}           headless DSP and engine checks
  audio/
    Voice.{h,cpp}            abstract base: the polymorphic interface
    FMVoice.{h,cpp}          carrier + modulator
    PercussionVoice.{h,cpp}  filtered noise + pitch envelope
    SamplerVoice.{h,cpp}     pitch-shifted WAV playback
    Envelope.{h,cpp}         ADSR              (composed into each Voice)
    Filter.{h,cpp}           resonant low-pass (composed into each Voice)
    SynthEngine.{h,cpp}      voice pool, note stealing, mixing, audioOut, device setup
    EventQueue.h             lock-free UI → audio thread event ring buffer
    SampleLibrary.{h,cpp}    shared sample store (aggregation)
    Sample.h                 immutable decoded PCM
    WavLoader.{h,cpp}        minimal 16-bit PCM RIFF parser
    Note.h                   MIDI note / frequency helpers
    DspMath.h                shared DSP helpers and denormal handling
  ui/                        Sequencer, XYPad, Oscilloscope, ParticleField, StatusPanel
synth/bin/data/samples/      prepared one-shot WAVs (generated, not committed)
```
