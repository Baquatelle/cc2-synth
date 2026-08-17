#include "Sequencer.h"

#include "../audio/SynthEngine.h"

#include "ofGraphics.h"

#include <algorithm>

namespace
{

/// A minor pentatonic scale relative to the root, so anything the sequencer plays
/// (including a randomised pattern) stays musical.
constexpr int kScale[]   = {0, 3, 5, 7, 10};
constexpr int kScaleSize = 5;

constexpr int kRootNote = 57; // A3

ofColor colorFor(VoiceType aType)
{
    switch (aType)
    {
    case VoiceType::FM:
        return {90, 170, 235};
    case VoiceType::Percussion:
        return {235, 140, 70};
    case VoiceType::Sampler:
        return {150, 225, 130};
    }
    return {200, 200, 200};
}

} // namespace

void Sequencer::setupDefaultPattern()
{
    // Rows 0-2: drums.
    // Rows 3-4: sampled one-shots.
    // Rows 5-7: melodic FM.
    // Laid out low-to-high on screen by reversing the row order when drawing.
    for (std::size_t row = 0; row < kRows; ++row)
    {
        Row& target = mRows[row];
        target.mSteps.fill(false);

        if (row < 3)
        {
            target.mVoiceType = VoiceType::Percussion;
            // Percussion pitch selects the drum in the kit rather than a musical note.
            target.mMidiNote  = 36 + static_cast<int>(row);
        }
        else if (row < 5)
        {
            target.mVoiceType = VoiceType::Sampler;
            target.mMidiNote  = kRootNote + 12 + static_cast<int>(row - 3) * 3;
        }
        else
        {
            target.mVoiceType = VoiceType::FM;
            const int degree  = static_cast<int>(row - 5);
            target.mMidiNote  = kRootNote + 12 + kScale[degree % kScaleSize];
        }
    }

    // A simple, recognisable starting groove: kick on the quarters, snare on the
    // backbeat, hats on the offbeats, plus a sparse melodic figure.
    for (std::size_t step = 0; step < kSteps; step += 4)
    {
        mRows[0].mSteps[step] = true;
    }
    mRows[1].mSteps[4]  = true;
    mRows[1].mSteps[12] = true;
    for (std::size_t step = 2; step < kSteps; step += 4)
    {
        mRows[2].mSteps[step] = true;
    }
    mRows[5].mSteps[0]  = true;
    mRows[5].mSteps[6]  = true;
    mRows[6].mSteps[10] = true;
    mRows[7].mSteps[14] = true;
}

void Sequencer::setTempo(float aBpm)
{
    mBpm = dsp::clampf(aBpm, 40.0f, 240.0f);
}

float Sequencer::secondsPerStep() const
{
    // 16 steps per bar == sixteenth notes at four beats per bar.
    return 60.0f / (mBpm * 4.0f);
}

void Sequencer::toggleStep(std::size_t aRow, std::size_t aStep)
{
    if (aRow < kRows && aStep < kSteps)
    {
        mRows[aRow].mSteps[aStep] = !mRows[aRow].mSteps[aStep];
    }
}

bool Sequencer::stepEnabled(std::size_t aRow, std::size_t aStep) const
{
    return (aRow < kRows && aStep < kSteps) ? mRows[aRow].mSteps[aStep] : false;
}

void Sequencer::advance(float aDeltaSeconds)
{
    if (aDeltaSeconds <= 0.0f)
    {
        return;
    }

    // Gates are released even while stopped, so pressing stop never strands a note.
    releaseExpiredNotes(aDeltaSeconds);

    if (!mPlaying)
    {
        return;
    }

    mStepTimer += aDeltaSeconds;
    const float stepDuration = secondsPerStep();

    // A while loop rather than an if: a long frame (window drag, a hitch) must not
    // silently swallow steps.
    while (mStepTimer >= stepDuration)
    {
        mStepTimer -= stepDuration;
        mCurrentStep = (mCurrentStep + 1) % kSteps;
        triggerStep(mCurrentStep);
    }
}

void Sequencer::triggerStep(std::size_t aStep)
{
    if (mEngine == nullptr)
    {
        return;
    }

    for (const Row& row : mRows)
    {
        if (!row.mSteps[aStep])
        {
            continue;
        }

        mEngine->noteOn(row.mVoiceType, row.mMidiNote, 0.9f, kSourceId);
        ++mTriggeredNotes;

        // Remember the note so its gate can be closed later. One-shot voices ignore
        // note-off, but queueing it anyway keeps this bookkeeping uniform.
        HeldNote held;
        held.mVoiceType        = row.mVoiceType;
        held.mMidiNote         = row.mMidiNote;
        held.mRemainingSeconds = secondsPerStep() * mGateRatio;
        mHeldNotes.push_back(held);
    }
}

void Sequencer::releaseExpiredNotes(float aDeltaSeconds)
{
    if (mHeldNotes.empty())
    {
        return;
    }

    for (HeldNote& held : mHeldNotes)
    {
        held.mRemainingSeconds -= aDeltaSeconds;
        if (held.mRemainingSeconds <= 0.0f && mEngine != nullptr)
        {
            mEngine->noteOff(held.mVoiceType, held.mMidiNote, kSourceId);
        }
    }

    mHeldNotes.erase(std::remove_if(mHeldNotes.begin(), mHeldNotes.end(),
                                    [](const HeldNote& aHeld) { return aHeld.mRemainingSeconds <= 0.0f; }),
                     mHeldNotes.end());
}

void Sequencer::draw(float aX, float aY, float aWidth, float aHeight) const
{
    const float cellWidth  = aWidth / static_cast<float>(kSteps);
    const float cellHeight = aHeight / static_cast<float>(kRows);

    for (std::size_t row = 0; row < kRows; ++row)
    {
        // Draw row 0 at the bottom: low sounds low, which is what a musician expects.
        const float cellY = aY + static_cast<float>(kRows - 1 - row) * cellHeight;

        for (std::size_t step = 0; step < kSteps; ++step)
        {
            const float cellX      = aX + static_cast<float>(step) * cellWidth;
            const bool  onBeat     = (step % 4) == 0;
            const bool  isPlayhead = mPlaying && step == mCurrentStep;

            // Background: beat markers give the grid a readable pulse.
            ofSetColor(isPlayhead ? ofColor(70, 70, 80) : (onBeat ? ofColor(42) : ofColor(30)));
            ofDrawRectangle(cellX + 1.0f, cellY + 1.0f, cellWidth - 2.0f, cellHeight - 2.0f);

            if (mRows[row].mSteps[step])
            {
                ofColor color = colorFor(mRows[row].mVoiceType);
                if (isPlayhead)
                {
                    // Flash the cell as the playhead strikes it.
                    color.setBrightness(std::min(255, static_cast<int>(color.getBrightness()) + 70));
                }
                ofSetColor(color);
                ofDrawRectRounded(cellX + 2.0f, cellY + 2.0f, cellWidth - 4.0f, cellHeight - 4.0f, 3.0f);
            }
        }
    }

    // Playhead column outline, so the position is obvious even on empty steps.
    if (mPlaying)
    {
        ofNoFill();
        ofSetColor(200, 200, 120);
        ofDrawRectangle(aX + static_cast<float>(mCurrentStep) * cellWidth, aY, cellWidth, aHeight);
        ofFill();
    }

    ofSetColor(255);
}

bool Sequencer::cellAt(float aMouseX, float aMouseY, float aX, float aY, float aWidth, float aHeight,
                       std::size_t& aOutRow, std::size_t& aOutStep) const
{
    if (aMouseX < aX || aMouseX >= aX + aWidth || aMouseY < aY || aMouseY >= aY + aHeight)
    {
        return false;
    }

    const float cellWidth  = aWidth / static_cast<float>(kSteps);
    const float cellHeight = aHeight / static_cast<float>(kRows);
    const auto  step       = static_cast<std::size_t>((aMouseX - aX) / cellWidth);
    const auto  visualRow  = static_cast<std::size_t>((aMouseY - aY) / cellHeight);
    if (step >= kSteps || visualRow >= kRows)
    {
        return false;
    }

    // draw() puts row 0 at the bottom, so invert to get back to the data row.
    aOutRow  = kRows - 1 - visualRow;
    aOutStep = step;
    return true;
}

bool Sequencer::handleClick(float aMouseX, float aMouseY, float aX, float aY, float aWidth, float aHeight)
{
    std::size_t row  = 0;
    std::size_t step = 0;
    if (!cellAt(aMouseX, aMouseY, aX, aY, aWidth, aHeight, row, step))
    {
        return false;
    }

    toggleStep(row, step);
    return true;
}
