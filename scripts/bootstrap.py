#!/usr/bin/env python3
"""Bootstrap the synth build environment.

Idempotent: safe to re-run, skips whatever is already in place. Gets a clean
checkout to the point where scripts/build.py can produce a runnable binary.

Steps:
  1. Download openFrameworks 0.12.1 into <repo>/of/  (gitignored)
  2. Prepare synth/bin/data/samples/ with 16-bit PCM one-shots: four
     synthesized drums/pluck plus slices cut from the loops openFrameworks
     ships in examples/sound/  (no network, no third-party sample packs)
  3. Run the openFrameworks projectGenerator for the current platform
  4. Verify OF_ROOT in the generated config.make (used by the Windows build)

No addons are needed: the synth uses core openFrameworks only (ofSoundStream
for audio, immediate-mode drawing for the UI).

Unlike a capture app there is no macOS Info.plist patching to do here — the
synth only *outputs* audio, and macOS only gates audio *input* behind
NSMicrophoneUsageDescription.

Platforms: macOS (Xcode) and Windows (msys2/mingw64, run from an MSYS2 shell
or plain Python — only the download and the projectGenerator run here).
"""

import array
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import urllib.request
import wave
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OF_DIR = ROOT / "of"
APP_DIR = ROOT / "synth"
SAMPLES_DIR = APP_DIR / "bin" / "data" / "samples"
CACHE_DIR = ROOT / ".cache"

OF_VERSION = "0.12.1"
OF_RELEASE_BASE = f"https://github.com/openframeworks/openFrameworks/releases/download/{OF_VERSION}"
OF_ARCHIVES = {
    "darwin": f"of_v{OF_VERSION}_osx_release.tar.gz",
    "win32": f"of_v{OF_VERSION}_msys2_mingw64_release.zip",
}

# Loops bundled with openFrameworks' own sound examples. They are the only
# genuinely pre-recorded material we use, so the "sampling" feature needs no
# external download and carries no licensing ambiguity.
OF_SOUND_ASSETS = OF_DIR / "examples" / "sound" / "soundPlayerExample" / "bin" / "data" / "sounds"

SAMPLE_RATE = 44100
TWO_PI = 2.0 * math.pi

# Bump to force regeneration of bin/data/samples on the next bootstrap.
SAMPLE_SET_VERSION = "1"


def log(msg):
    print(f"[bootstrap] {msg}", flush=True)


def die(msg):
    print(f"[bootstrap] ERROR: {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def platform_key():
    if sys.platform == "darwin":
        return "darwin"
    if sys.platform in ("win32", "cygwin", "msys"):
        return "win32"
    die(f"unsupported platform: {sys.platform} (macOS and Windows only)")


_opener = urllib.request.build_opener()
_opener.addheaders = [("User-Agent", "cc2-synth-bootstrap/1.0 (university course project)")]
urllib.request.install_opener(_opener)


def download(url, dest: Path):
    """Download url to dest atomically, with a crude progress indicator."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    log(f"downloading {dest.name} ...")

    def hook(blocks, block_size, total):
        done = blocks * block_size
        if total > 0 and blocks % 400 == 0:
            print(f"    {done / 1e6:.0f} / {total / 1e6:.0f} MB", flush=True)

    urllib.request.urlretrieve(url, tmp, reporthook=hook)
    tmp.replace(dest)
    return dest


# ---------------------------------------------------------------- openFrameworks


def ensure_openframeworks(plat):
    if (OF_DIR / "libs" / "openFrameworks").is_dir():
        log(f"openFrameworks already present at {OF_DIR}")
        return

    archive_name = OF_ARCHIVES[plat]
    archive = CACHE_DIR / archive_name
    if not archive.exists():
        download(f"{OF_RELEASE_BASE}/{archive_name}", archive)
    else:
        log(f"using cached {archive_name}")

    log(f"extracting {archive_name} ...")
    extract_dir = CACHE_DIR / "of_extract"
    if extract_dir.exists():
        shutil.rmtree(extract_dir)
    extract_dir.mkdir(parents=True)

    try:
        if archive_name.endswith(".tar.gz"):
            # shell tar: much faster than the tarfile module and keeps exec bits
            subprocess.run(["tar", "xzf", str(archive), "-C", str(extract_dir)], check=True)
        else:
            with zipfile.ZipFile(archive) as z:
                z.extractall(extract_dir)
    except (subprocess.CalledProcessError, zipfile.BadZipFile, EOFError):
        die(f"could not extract {archive} — the cached download is probably "
            f"incomplete or corrupt. Delete it and re-run this script.")

    roots = [p for p in extract_dir.iterdir() if p.is_dir()]
    if len(roots) != 1:
        die(f"expected one top-level dir in {archive_name}, got: {[p.name for p in roots]}")
    roots[0].rename(OF_DIR)
    shutil.rmtree(extract_dir, ignore_errors=True)
    log(f"openFrameworks {OF_VERSION} installed at {OF_DIR}")


# ------------------------------------------------------------------- wav helpers


def read_wav_mono(path: Path):
    """Read a PCM wav into (mono float samples in [-1,1], sample rate).

    Handles 8/16/24/32-bit PCM and downmixes to mono by averaging channels.
    Written by hand rather than with audioop, which was removed from the
    standard library in Python 3.13.
    """
    with wave.open(str(path), "rb") as w:
        channels = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())

    if width == 1:
        # 8-bit wav is unsigned, with 128 as silence
        values = [(b - 128) / 128.0 for b in raw]
    elif width == 2:
        count = len(raw) // 2
        values = [v / 32768.0 for v in struct.unpack(f"<{count}h", raw[: count * 2])]
    elif width == 3:
        values = [
            int.from_bytes(raw[i : i + 3], "little", signed=True) / 8388608.0
            for i in range(0, len(raw) - 2, 3)
        ]
    elif width == 4:
        count = len(raw) // 4
        values = [v / 2147483648.0 for v in struct.unpack(f"<{count}i", raw[: count * 4])]
    else:
        die(f"unsupported sample width {width * 8}-bit in {path.name}")

    if channels > 1:
        mono = [
            sum(values[i : i + channels]) / channels
            for i in range(0, len(values) - channels + 1, channels)
        ]
    else:
        mono = values
    return mono, rate


def write_wav_mono16(path: Path, samples, rate):
    """Write float samples to a mono 16-bit PCM wav (what our WavLoader reads)."""
    clamped = (max(-1.0, min(1.0, s)) for s in samples)
    pcm = array.array("h", (int(s * 32767.0) for s in clamped))
    if sys.byteorder == "big":
        pcm.byteswap()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def normalize(samples, peak):
    loudest = max((abs(s) for s in samples), default=0.0)
    if loudest <= 1e-9:
        return list(samples)
    gain = peak / loudest
    return [s * gain for s in samples]


def fade(samples, rate, fade_in_s, fade_out_s):
    """Fade the edges so a one-shot starts and ends at exactly zero.

    Without this the sampler produces an audible click whenever a slice starts
    or stops part-way through a waveform.
    """
    out = list(samples)
    total = len(out)
    n_in = min(int(fade_in_s * rate), total // 2)
    n_out = min(int(fade_out_s * rate), total // 2)
    for i in range(n_in):
        out[i] *= i / n_in
    for i in range(n_out):
        out[total - 1 - i] *= i / n_out
    return out


# -------------------------------------------------------------- synthesized hits


def make_kick():
    """Sine with an exponential pitch sweep — the classic synthesized kick."""
    length = int(0.42 * SAMPLE_RATE)
    out = []
    phase = 0.0
    for i in range(length):
        t = i / SAMPLE_RATE
        freq = 45.0 + 95.0 * math.exp(-t / 0.035)
        phase += TWO_PI * freq / SAMPLE_RATE
        body = math.sin(phase) * math.exp(-t / 0.12)
        click = 0.35 * math.exp(-t / 0.002)
        out.append(body + click)
    return out


def make_snare():
    """Noise burst (differentiated for brightness) over two tuned body tones."""
    rng = random.Random(20260818)
    length = int(0.28 * SAMPLE_RATE)
    out = []
    previous = 0.0
    phase_low = 0.0
    phase_high = 0.0
    for i in range(length):
        t = i / SAMPLE_RATE
        white = rng.uniform(-1.0, 1.0)
        bright = white - previous
        previous = white
        phase_low += TWO_PI * 185.0 / SAMPLE_RATE
        phase_high += TWO_PI * 331.0 / SAMPLE_RATE
        body = (math.sin(phase_low) + 0.7 * math.sin(phase_high)) * math.exp(-t / 0.045)
        out.append(bright * 0.75 * math.exp(-t / 0.075) + body * 0.45)
    return out


def make_hat():
    """Twice-differentiated noise (a steep highpass) with a very fast decay."""
    rng = random.Random(776347)
    length = int(0.12 * SAMPLE_RATE)
    out = []
    previous = 0.0
    previous_diff = 0.0
    for i in range(length):
        t = i / SAMPLE_RATE
        white = rng.uniform(-1.0, 1.0)
        diff = white - previous
        previous = white
        out.append((diff - previous_diff) * math.exp(-t / 0.018))
        previous_diff = diff
    return out


def make_pluck():
    """Karplus-Strong string: a noise burst circulating through a damped delay."""
    rng = random.Random(4407)
    freq = 220.0
    length = int(1.1 * SAMPLE_RATE)
    delay = max(2, int(SAMPLE_RATE / freq))
    buffer = [rng.uniform(-1.0, 1.0) for _ in range(delay)]
    out = []
    index = 0
    for _ in range(length):
        current = buffer[index]
        following = buffer[(index + 1) % delay]
        buffer[index] = 0.5 * (current + following) * 0.996
        index = (index + 1) % delay
        out.append(current)
    return out


# ------------------------------------------------------------------ loop slicing


def find_onsets(samples, rate):
    """Return frame offsets of percussive onsets in a loop.

    Peak-per-window envelope, then rising edges through a threshold relative to
    the loop's own peak, with a minimum gap so one hit is not reported twice.
    """
    window = max(1, int(0.004 * rate))
    peaks = [
        max(abs(s) for s in samples[start : start + window])
        for start in range(0, len(samples) - window, window)
    ]
    if not peaks:
        return []
    loudest = max(peaks)
    if loudest <= 1e-9:
        return []

    threshold = loudest * 0.30
    min_gap = max(1, int(0.09 * rate / window))
    onsets = []
    last = -min_gap
    for i, peak in enumerate(peaks):
        previous = peaks[i - 1] if i > 0 else 0.0
        if peak >= threshold and previous < threshold and (i - last) >= min_gap:
            onsets.append(i * window)
            last = i
    return onsets


def slice_loop_into_hits(source: Path, count):
    """Cut up to `count` one-shots out of a drum loop, loudest-usable first."""
    mono, rate = read_wav_mono(source)
    hits = []
    length = int(0.32 * rate)
    for start in find_onsets(mono, rate)[:count]:
        segment = mono[start : start + length]
        if len(segment) < length:
            segment = segment + [0.0] * (length - len(segment))
        hits.append((fade(normalize(segment, 0.9), rate, 0.002, 0.02), rate))
    return hits


def take_head(source: Path, seconds):
    """Take the first `seconds` of a sound as a pitched sampler note."""
    mono, rate = read_wav_mono(source)
    head = mono[: min(len(mono), int(seconds * rate))]
    return fade(normalize(head, 0.9), rate, 0.003, 0.05), rate


# -------------------------------------------------------------------- sample set


def prepare_samples():
    """Build bin/data/samples. Files are numbered so SampleLibrary gets a stable
    order from a plain sorted directory listing; the C++ side strips the prefix
    for display."""
    stamp = SAMPLES_DIR / ".prepared"
    if stamp.exists() and stamp.read_text().strip() == SAMPLE_SET_VERSION:
        log("samples already prepared")
        return

    SAMPLES_DIR.mkdir(parents=True, exist_ok=True)
    log("preparing samples ...")

    synthesized = [
        ("01_kick.wav", make_kick(), 0.010),
        ("02_snare.wav", make_snare(), 0.008),
        ("03_hat.wav", make_hat(), 0.004),
        ("04_pluck.wav", make_pluck(), 0.060),
    ]
    for name, samples, fade_out in synthesized:
        shaped = fade(normalize(samples, 0.9), SAMPLE_RATE, 0.001, fade_out)
        write_wav_mono16(SAMPLES_DIR / name, shaped, SAMPLE_RATE)
        log(f"  synthesized {name}")

    # Recorded material: slices of openFrameworks' own bundled loops. Missing
    # assets are a warning, not an error — the synthesized kit alone is enough
    # to run the app.
    beat = OF_SOUND_ASSETS / "beat.wav"
    if beat.exists():
        for i, (samples, rate) in enumerate(slice_loop_into_hits(beat, 4)):
            name = f"{5 + i:02d}_beat_hit_{i + 1}.wav"
            write_wav_mono16(SAMPLES_DIR / name, samples, rate)
            log(f"  sliced {name} from beat.wav")
    else:
        log(f"  warning: {beat} not found, skipping beat slices")

    synth_note = OF_SOUND_ASSETS / "synth.wav"
    if synth_note.exists():
        samples, rate = take_head(synth_note, 1.2)
        write_wav_mono16(SAMPLES_DIR / "09_synth_note.wav", samples, rate)
        log("  extracted 09_synth_note.wav from synth.wav")
    else:
        log(f"  warning: {synth_note} not found, skipping synth note")

    stamp.write_text(SAMPLE_SET_VERSION + "\n")
    log(f"samples ready in {SAMPLES_DIR}")


# --------------------------------------------------------------- projectGenerator


def find_project_generator(plat):
    if plat == "darwin":
        candidates = list(OF_DIR.glob(
            "projectGenerator*/projectGenerator.app/Contents/Resources/app/app/projectGenerator"))
    else:
        # the CLI binary lives under resources/; the top-level exe is the GUI shell
        candidates = list(OF_DIR.glob("projectGenerator*/resources/app/app/projectGenerator.exe"))
        candidates += list(OF_DIR.glob("projectGenerator*/projectGenerator.exe"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    die(f"projectGenerator CLI not found under {OF_DIR}")


def run_project_generator(plat):
    generator = find_project_generator(plat)
    of_platform = "osx" if plat == "darwin" else "msys2"
    log(f"running projectGenerator ({of_platform}) on {APP_DIR} ...")
    subprocess.run(
        [str(generator), f"-o{OF_DIR}", f"-p{of_platform}", str(APP_DIR)],
        check=True,
    )


def ensure_makefile_of_root():
    """Make sure config.make's OF_ROOT points at our openFrameworks install.

    The app lives at <repo>/synth, beside <repo>/of, rather than inside
    of/apps/myApps/ where the stock relative default (../../..) would apply.
    macOS builds from the Xcode project and ignores this file, so a wrong value
    only shows up on Windows as a confusing 'no rule to make target' failure.
    """
    config = APP_DIR / "config.make"
    if not config.exists():
        return

    wanted = Path(os.path.relpath(OF_DIR, APP_DIR)).as_posix()
    lines = config.read_text().splitlines()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped.startswith("OF_ROOT") or "=" not in stripped:
            continue
        current = stripped.split("=", 1)[1].strip()
        resolved = Path(current) if os.path.isabs(current) else APP_DIR / current
        if (resolved / "libs" / "openFrameworks").is_dir():
            return
        lines[i] = f"OF_ROOT = {wanted}"
        config.write_text("\n".join(lines) + "\n")
        log(f"fixed OF_ROOT in config.make -> {wanted}")
        return

    lines.append(f"OF_ROOT = {wanted}")
    config.write_text("\n".join(lines) + "\n")
    log(f"set OF_ROOT in config.make -> {wanted}")


def main():
    plat = platform_key()
    CACHE_DIR.mkdir(exist_ok=True)
    ensure_openframeworks(plat)
    prepare_samples()
    run_project_generator(plat)
    ensure_makefile_of_root()
    log("done. Next: python3 scripts/build.py")


if __name__ == "__main__":
    main()
