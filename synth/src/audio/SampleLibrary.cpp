#include "SampleLibrary.h"

#include "WavLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{

/// Fallback pitch for samples whose filename carries no `_n<midi>` suffix.
constexpr float kDefaultBaseMidiNote = 60.0f;

/// Extracts the base MIDI note from a trailing `_n<number>` in the file stem,
/// e.g. "04_pluck_n57" -> 57.
float parseBaseMidiNote(const std::string& aStem)
{
    const std::size_t marker = aStem.rfind("_n");
    if (marker == std::string::npos || marker + 2 >= aStem.size())
    {
        return kDefaultBaseMidiNote;
    }

    const std::string digits = aStem.substr(marker + 2);
    if (!std::all_of(digits.begin(), digits.end(), [](unsigned char aC) { return std::isdigit(aC) != 0; }))
    {
        return kDefaultBaseMidiNote;
    }

    const int value = std::atoi(digits.c_str());
    if (value < 0 || value > 127)
    {
        return kDefaultBaseMidiNote;
    }
    return static_cast<float>(value);
}

bool hasWavExtension(const std::filesystem::path& aPath)
{
    std::string extension = aPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char aC) { return static_cast<char>(std::tolower(aC)); });
    return extension == ".wav";
}

} // namespace

std::size_t SampleLibrary::loadDirectory(const std::string& aDirectory)
{
    clear();

    std::error_code error;
    if (!std::filesystem::is_directory(aDirectory, error))
    {
        std::cerr << "[samples] no sample directory at " << aDirectory << " -- sampler voice will stay silent\n";
        return 0;
    }

    // Collect first, then sort: directory iteration order is unspecified and
    // differs between macOS and Windows, and the sequencer addresses samples by
    // index. A stable order keeps a saved pattern sounding the same everywhere.
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(aDirectory, error))
    {
        if (entry.is_regular_file() && hasWavExtension(entry.path()))
        {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());

    for (const auto& path : paths)
    {
        auto        sample = std::make_shared<Sample>();
        std::string loadError;
        if (!wav::load(path.string(), *sample, loadError))
        {
            std::cerr << "[samples] skipping " << path.filename().string() << ": " << loadError << "\n";
            continue;
        }

        const std::string stem = path.stem().string();
        sample->mName          = stem;
        sample->mBaseMidiNote  = parseBaseMidiNote(stem);
        mSamples.push_back(std::move(sample));
    }

    std::cout << "[samples] loaded " << mSamples.size() << " sample(s) from " << aDirectory << "\n";
    return mSamples.size();
}

void SampleLibrary::add(std::shared_ptr<const Sample> aSample)
{
    if (aSample && !aSample->empty())
    {
        mSamples.push_back(std::move(aSample));
    }
}

void SampleLibrary::clear()
{
    // Any voice still holding a shared_ptr keeps its own sample alive; only the
    // library's references are dropped here. That is the aggregation guarantee.
    mSamples.clear();
}

std::shared_ptr<const Sample> SampleLibrary::at(std::size_t aIndex) const
{
    if (mSamples.empty())
    {
        return nullptr;
    }
    return mSamples[aIndex % mSamples.size()];
}

std::vector<std::string> SampleLibrary::names() const
{
    std::vector<std::string> result;
    result.reserve(mSamples.size());
    for (const auto& sample : mSamples)
    {
        result.push_back(sample->mName);
    }
    return result;
}
