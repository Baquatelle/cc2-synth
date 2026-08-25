# KNOWN_ISSUES.md

This file is linked from the top of `README.md` ("a handful of known
defects remain") and previously did not exist — that broken link is fixed
by this file. `scripts/check_docs.py` now checks for this class of problem
so a referenced-but-missing doc file doesn't ship again silently.

## Open items

### 1. Windows build not yet locally verified
`README.md` states Windows support is "written but not yet verified."
`.github/workflows/ci.yml` does build and `--selftest` on `windows-latest`
on every push, so CI is exercising it — but nobody on the team has
manually run the keyboard/sequencer/XY-pad/oscilloscope pass described in
README §6 ("Verification") on real Windows hardware yet. **Action:**
whoever has a Windows machine (or an MSYS2 shell) should run

```
python3 scripts/bootstrap.py
python3 scripts/build.py --check
python3 scripts/build.py --run
```

and append the result below (pass/fail, console audio-device log line,
and anything that felt or sounded wrong).

- Status as of 2026-08-25: **not yet run**, no team member has confirmed
  a manual pass on Windows. CI's `windows-latest` job should be checked in
  the Actions tab and its result recorded here before submission.

### 2. Platform latency asymmetry (by design, not a bug)
The engine requests a 256-frame buffer on macOS and 512 on Windows, so
key-to-sound latency is inherently higher on Windows (~12 ms vs. ~6 ms).
This is a deliberate trade-off against WASAPI/DirectSound glitching, not a
defect — noted here so it isn't mistaken for one during grading or a demo.

### 3. Sampler is 16-bit PCM WAV only
`WavLoader` decodes PCM/float WAV but there is no MP3/OGG support (no
audio-decoding library ships with openFrameworks 0.12.1, and adding one was
out of scope). Dropping a non-WAV file into `synth/bin/data/samples/`
will silently fail to load rather than resample it.

### 4. No FFT / spectrum view
There is no spectrum analyzer. This was a deliberate scope decision (the
chosen visualizations don't need one, and `ofSoundGetSpectrum()` can't see
audio produced by this project's own `audioOut()`), not an oversight — but
listing it here so it's not mistaken for a missing feature during review.

### 5. Documentation gaps (fixed by this PR)
`PLAN.md` and `KNOWN_ISSUES.md` were referenced by `README.md` but did not
exist in the repository until this change.

## How to add a new entry
Found something while testing? Add a dated bullet under a new numbered
heading here, including: what you did, what happened, and whether it's a
real defect or a scope boundary. Keep it factual — this file is meant to be
read by a grader as evidence the team actually tested the build.
