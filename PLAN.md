# PLAN.md — cc2-synth project plan

**Course project:** Software synthesizer in openFrameworks (CODE Interactivity, Project submission "GuineaPig")
**Team:** Abel Gabor ([@Baquatelle](https://github.com/Baquatelle)), Mohammadali Moghadam ([@s001082MohammadaliMoghadam](https://github.com/s001082MohammadaliMoghadam))
**Repository:** https://github.com/Baquatelle/cc2-synth
**Expected workload:** ~12h total (5h Week 5, 7h Week 6)

## 1. Goal

Build a polyphonic software synthesizer directly on top of openFrameworks'
raw audio callback (no `ofSoundPlayer`), satisfying the assignment's core
requirement (3+ playable sounds, keyboard interaction) plus the listed
"pluses": visualization, non-keyboard interaction, and sampling.

## 2. Scope we agreed on

- **Three sound sources**, each a distinct `Voice` subclass so the engine's
  mixing/pooling code never special-cases a timbre:
  - FM (carrier + modulator, sustaining while held)
  - Percussion (filtered noise + swept body, one-shot)
  - Sampler (pitch-shifted playback of pre-recorded WAV one-shots)
- **Interaction beyond the keyboard**: a clickable/paintable step sequencer
  and a mouse XY pad (filter cutoff / resonance / FM morph).
- **Visualization**: a live oscilloscope on the master output, a
  note-triggered particle field, and a status panel showing live engine
  state (also what lets `--selftest` run with zero UI objects instantiated).
- **OOP structure**: composition (`Voice` owns its `Envelope`/`Filter`),
  aggregation (`SamplerVoice` shares a `Sample` via `shared_ptr<const Sample>`
  from `SampleLibrary`), association (UI objects hold a non-owning reference
  to `SynthEngine`), and inheritance/polymorphism across the `Voice`
  hierarchy. Rationale for each is written up in `README.md` §3.
- **Cross-platform**: one source tree, native projects generated per
  platform by openFrameworks' `projectGenerator`; CI builds and
  self-tests both macOS and Windows on every push
  (`.github/workflows/ci.yml`).

## 3. Milestones (as actually delivered, in commit order)

1. Project skeleton, build scripts, DSP math helpers, ADSR envelope,
   state-variable filter (Aug 14–15)
2. `Voice` base class + `FMVoice`, engine + voice pool, keyboard input,
   first audible sound (Aug 15)
3. `PercussionVoice`, note stealing, panic, master soft-clip (Aug 15)
4. Sampling: `Sample`, RIFF/WAV parser, `SampleLibrary`,
   `SamplerVoice` (Aug 15–16)
5. UI layer: oscilloscope, XY pad, particle field, sequencer, status
   panel (Aug 16–17)
6. Robustness pass: device negotiation, pool rebuild only while the
   stream is closed (Aug 17)
7. Headless `--selftest` harness covering envelope/filter, RIFF
   parsing, voice pooling/stealing, sample sharing, gain staging,
   one-shot vs. sustaining release, sequencer timing (Aug 17)
8. CI for macOS + Windows, architecture write-up (Aug 17–19)
9. **This pass** (Aug 25): fill the two documentation gaps this plan
   itself closes (`PLAN.md`, `KNOWN_ISSUES.md`), add a docs-link
   checker so a broken reference like this can't silently ship again,
   and record Windows build verification results.

## 4. Division of work

- Abel: engine/DSP core (`Voice`, `FMVoice`, `PercussionVoice`,
  `SamplerVoice`, `SynthEngine`, `Envelope`, `Filter`, `SampleLibrary`,
  `WavLoader`), UI layer, CI, architecture documentation.
- Mohammadali: documentation completeness pass (this PR), Windows build
  verification, AI-usage documentation, project manual/PDF for
  submission.

## 5. Remaining before submission

- [ ] Confirm the Windows CI job is green (or capture what fails, in
      `KNOWN_ISSUES.md`) and/or verify a local Windows build.
- [ ] Export the project manual (controls, quickstart, architecture
      summary) as the required submission PDF.
- [ ] Finish `AI_USAGE.md` with each team member's real prompts.
- [ ] Package source + PDF + learning log into the submission zip.
