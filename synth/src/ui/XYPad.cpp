#include "XYPad.h"

#include <string>

#include "../audio/DspMath.h"
#include "../audio/SynthEngine.h"

#include "ofGraphics.h"

namespace
{

constexpr float kMinCutoff = 120.0f;
constexpr float kMaxCutoff = 12000.0f;
} // namespace

bool XYPad::handleDrag(float aMouseX, float aMouseY, float aX, float aY, float aWidth, float aHeight)
{
    if (aMouseX < aX || aMouseX >= aX + aWidth || aMouseY < aY || aMouseY >= aY + aHeight)
    {
        return false;
    }

    mNormalizedX = dsp::clampf((aMouseX - aX) / aWidth, 0.0f, 1.0f);

    // Screen Y grows downward; invert so "up" means "more".
    mNormalizedY = dsp::clampf(1.0f - (aMouseY - aY) / aHeight, 0.0f, 1.0f);

    if (mEngine != nullptr)
    {
        // Exponential frequency mapping: a linear sweep would spend most of the
        // pad's width in the range where changes are barely audible.
        const float cutoff = kMinCutoff * std::pow(kMaxCutoff / kMinCutoff, mNormalizedX);
        mEngine->setCutoff(cutoff);

        // Y opens the filter and pushes the morph together, so one vertical
        // gesture is audibly doing something on every voice type. Resonance stops
        // short of self-oscillation to keep the output civilised.
        mEngine->setResonance(mNormalizedY * 0.9f);
        mEngine->setMorph(mNormalizedY);
    }

    return true;
}

void XYPad::draw(float aX, float aY, float aWidth, float aHeight) const
{
    ofSetColor(28);
    ofDrawRectangle(aX, aY, aWidth, aHeight);

    // Grid, to make the pad legible as a 2D control surface.
    ofSetColor(46);
    for (int i = 1; i < 4; ++i)
    {
        const float fraction = static_cast<float>(i) / 4.0f;
        ofDrawLine(aX + aWidth * fraction, aY, aX + aWidth * fraction, aY + aHeight);
        ofDrawLine(aX, aY + aHeight * fraction, aX + aWidth, aY + aHeight * fraction);
    }

    const float pointX = aX + mNormalizedX * aWidth;
    const float pointY = aY + (1.0f - mNormalizedY) * aHeight;

    ofSetColor(90, 160, 200);
    ofDrawLine(pointX, aY, pointX, aY + aHeight);
    ofDrawLine(aX, pointY, aX + aWidth, pointY);

    ofSetColor(200, 240, 255);
    ofDrawCircle(pointX, pointY, 5.0f);

    ofSetColor(120);

    // The std::string casts are required, not stylistic: openFrameworks 0.12.1
    // declares ofDrawBitmapString() as a template that forwards to ofToString(),
    // and a raw literal deduces T = char[N], which has no instantiation and so
    // fails only at link time with an undefined ofToString<char [N]> symbol.
    ofDrawBitmapString(std::string("cutoff >"), aX + 6.0f, aY + aHeight - 8.0f);
    ofDrawBitmapString(std::string("morph ^"), aX + 6.0f, aY + 14.0f);
}
