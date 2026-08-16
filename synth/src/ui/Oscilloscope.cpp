#include "Oscilloscope.h"

#include "../audio/SynthEngine.h"

#include "ofGraphics.h"
#include "ofPolyline.h"

#include <algorithm>

void Oscilloscope::update()
{
    if (mEngine != nullptr)
    {
        mEngine->copyScope(mSamples);
    }
}

namespace
{

/// Finds a rising zero crossing to start the trace from.
std::size_t findTrigger(const std::vector<float>& aSamples)
{
    if (aSamples.size() < 4)
    {
        return 0;
    }

    const std::size_t limit = aSamples.size() / 2;
    for (std::size_t i = 1; i < limit; ++i)
    {
        if (aSamples[i - 1] <= 0.0f && aSamples[i] > 0.0f)
        {
            return i;
        }
    }
    return 0; // silence, or no crossing found: draw from the start
}

} // namespace

void Oscilloscope::draw(float aX, float aY, float aWidth, float aHeight) const
{
    const std::size_t trigger = findTrigger(mSamples);

    // Frame and centre line, drawn even when idle so the panel never looks broken.
    ofSetColor(30);
    ofDrawRectangle(aX, aY, aWidth, aHeight);

    const float centreY = aY + aHeight * 0.5f;
    ofSetColor(60);
    ofDrawLine(aX, centreY, aX + aWidth, centreY);

    if (mSamples.empty())
    {
        return;
    }

    // Downsample to roughly one point per pixel: drawing 2048 vertices across a
    // few hundred pixels costs more than it shows.
    const auto  pointCount = static_cast<std::size_t>(std::max(2.0f, std::min(aWidth, 512.0f)));
    const float stride     = static_cast<float>(mSamples.size()) / static_cast<float>(pointCount);

    ofPolyline line;
    line.resize(pointCount);

    for (std::size_t i = 0; i < pointCount; ++i)
    {
        // Half the buffer is drawn, which leaves headroom for the trigger offset
        // (findTrigger only ever returns an index in the first half).
        const auto sampleIndex = static_cast<std::size_t>(static_cast<float>(i) * stride * 0.5f);

        // Offset by the trigger so the trace is phase-locked frame to frame.
        const float value  = mSamples[std::min(trigger + sampleIndex, mSamples.size() - 1)];
        const float pointX = aX + (static_cast<float>(i) / static_cast<float>(pointCount - 1)) * aWidth;
        const float pointY = centreY - value * (aHeight * 0.45f);
        line[i]            = {pointX, pointY, 0.0f};
    }

    ofSetColor(120, 230, 200);
    line.draw();

    // Peak meter along the bottom edge.
    if (mEngine != nullptr)
    {
        const float peak = std::min(mEngine->peakLevel(), 1.0f);
        ofSetColor(peak > 0.95f ? ofColor(255, 90, 90) : ofColor(90, 180, 160));
        ofDrawRectangle(aX, aY + aHeight - 3.0f, aWidth * peak, 3.0f);
    }
}
