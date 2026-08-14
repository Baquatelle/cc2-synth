#!/usr/bin/env python3
"""Bootstrap the synth build environment.

Idempotent: safe to re-run, skips whatever is already in place. Gets a clean
checkout to the point where scripts/build.py can produce a runnable binary.

Steps:
  1. Download openFrameworks 0.12.1 into <repo>/of/  (gitignored)
  2. Run the openFrameworks projectGenerator for the current platform
  3. Verify OF_ROOT in the generated config.make (used by the Windows build)

No addons are needed: the synth uses core openFrameworks only (ofSoundStream
for audio, immediate-mode drawing for the UI).

Unlike a capture app there is no macOS Info.plist patching to do here — the
synth only *outputs* audio, and macOS only gates audio *input* behind
NSMicrophoneUsageDescription.

Platforms: macOS (Xcode) and Windows (msys2/mingw64, run from an MSYS2 shell
or plain Python — only the download and the projectGenerator run here).
"""

import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OF_DIR = ROOT / "of"
APP_DIR = ROOT / "synth"
CACHE_DIR = ROOT / ".cache"

OF_VERSION = "0.12.1"
OF_RELEASE_BASE = f"https://github.com/openframeworks/openFrameworks/releases/download/{OF_VERSION}"
OF_ARCHIVES = {
    "darwin": f"of_v{OF_VERSION}_osx_release.tar.gz",
    "win32": f"of_v{OF_VERSION}_msys2_mingw64_release.zip",
}


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
    run_project_generator(plat)
    ensure_makefile_of_root()
    log("done. Next: python3 scripts/build.py")


if __name__ == "__main__":
    main()
