#pragma once

#include "Sample.h"

#include <string>

/// Minimal RIFF/WAVE reader.
///
/// openFrameworks 0.12.1 has no `ofSoundFile` class and ships no audio decoding
/// libraries (no libsndfile/vorbis/mpg123), and `ofSoundPlayer` exposes no way to
/// reach raw PCM. Since the sampler voice mixes inside our own `audioOut()`, we
/// need the samples as plain floats -- hence this parser.
namespace wav
{

/// Loads `aPath` into `aOutSample`. Returns false and fills `aOutError` on failure.
/// `aOutSample.mName` and `mBaseMidiNote` are left untouched; the caller owns those
/// naming conventions.
bool load(const std::string& aPath, Sample& aOutSample, std::string& aOutError);

} // namespace wav
